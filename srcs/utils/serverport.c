#include "utils/serverport.h"
#include "customcom/customcom.h"
#include "utils/utils.h"
#include "roleutil/role_sync_parse.h"
#include <concord/log.h>
#include <inttypes.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUILD_A_ID 1155152569040130101ULL // cfmain magic
#define GUILD_B_ID 1548621496052744203ULL // cfback magic

void rolesync_db_init(void) {
  sqlite3 *db = customcom_get_db();
  sqlite3_exec(db,
               "CREATE TABLE IF NOT EXISTS role_sync_map ("
               "guild_a_role_id INTEGER PRIMARY KEY, guild_b_role_id INTEGER "
               "NOT NULL, label TEXT);",
               NULL, NULL, NULL);
}
static void rolesync_send_chunked(struct discord *client,
                                  u64snowflake channel_id, const char *text) {
  size_t len = strlen(text);
  size_t pos = 0;

  while (pos < len) {
    size_t remaining = len - pos;
    size_t take = remaining > 1900 ? 1900 : remaining;
    size_t cut = take;

    if (take == 1900) {
      for (size_t i = take; i > 0; i--) {
        if (text[pos + i - 1] == '\n') {
          cut = i;
          break;
        }
      }
    }

    char *block = malloc(cut + 1);
    memcpy(block, text + pos, cut);
    block[cut] = '\0';

    struct discord_create_message params = {.content = block};
    discord_create_message(client, channel_id, &params, NULL);

    free(block);
    pos += cut;
  }
}
static void rolesync_map(struct discord *client,
                         const struct discord_message *event,
                         const char *args) {
  u64snowflake a_id = 0, b_id = 0;
  const char *label_text = NULL;
  char label[64] = "";

  if (!rolesync_parse_pair(args, &a_id, &b_id, &label_text) ||
      (label_text && strlen(label_text) >= sizeof label)) {
    struct discord_create_message reply = {
        .content = "usage: +rolesync map <roleA_id> <roleB_id> [label]"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  if (a_id == GUILD_A_ID || b_id == GUILD_B_ID) {
    struct discord_create_message reply = {
        .content = "The @everyone role cannot be mapped."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  struct discord_roles roles_a = {0}, roles_b = {0};
  struct discord_ret_roles ret_a = {.sync = &roles_a};
  struct discord_ret_roles ret_b = {.sync = &roles_b};
  if (discord_get_guild_roles(client, GUILD_A_ID, &ret_a) != CCORD_OK ||
      discord_get_guild_roles(client, GUILD_B_ID, &ret_b) != CCORD_OK) {
    struct discord_create_message reply = {
        .content = "Unable to verify roles with Discord. No mapping was saved."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  bool source_found = false, target_found = false, target_is_admin = false;
  for (int i = 0; i < roles_a.size; i++) {
    if (roles_a.array[i].id == a_id)
      source_found = true;
  }
  for (int i = 0; i < roles_b.size; i++) {
    if (roles_b.array[i].id == b_id) {
      target_found = true;
      target_is_admin =
          (roles_b.array[i].permissions & DISCORD_PERM_ADMINISTRATOR) != 0;
      break;
    }
  }

  if (!source_found || !target_found || target_is_admin) {
    struct discord_create_message reply = {
        .content = target_is_admin
                       ? "Administrator roles cannot be granted by role sync."
                       : "Both role IDs must exist in their respective sync guilds."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(
          db,
          "INSERT INTO role_sync_map (guild_a_role_id, guild_b_role_id, label) "
          "VALUES (?, ?, ?) "
          "ON CONFLICT(guild_a_role_id) DO UPDATE SET guild_b_role_id = "
          "excluded.guild_b_role_id, label = excluded.label",
          -1, &stmt, NULL) != SQLITE_OK) {
    struct discord_create_message reply = {
        .content = "Database error: role mapping was not saved."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)a_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)b_id);
  if (label_text) {
    snprintf(label, sizeof label, "%s", label_text);
    sqlite3_bind_text(stmt, 3, label, -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 3);
  }
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    struct discord_create_message reply = {
        .content = "Database error: role mapping was not saved."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  log_info("rolesync mapping updated by user %" PRIu64
           " in guild %" PRIu64 ": %" PRIu64 " -> %" PRIu64,
           event->author->id, event->guild_id, a_id, b_id);
  struct discord_create_message reply = {.content = "role mapping saved."};
  discord_create_message(client, event->channel_id, &reply, NULL);
}

static void rolesync_unmap(struct discord *client,
                           const struct discord_message *event,
                           const char *args) {
  u64snowflake a_id = 0;
  if (!rolesync_parse_u64(args, &a_id)) {
    struct discord_create_message reply = {
        .content = "usage: +rolesync unmap <roleA_id>"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "DELETE FROM role_sync_map WHERE guild_a_role_id = ?",
                     -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)a_id);
  sqlite3_step(stmt);
  int changed = sqlite3_changes(db);
  sqlite3_finalize(stmt);

  struct discord_create_message reply = {
      .content =
          changed > 0 ? "mapping removed." : "no mapping found for that role"};
  discord_create_message(client, event->channel_id, &reply, NULL);
}

static void rolesync_list(struct discord *client,
                          const struct discord_message *event,
                          const char *args) {
  (void)args;
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db,
                     "SELECT guild_a_role_id, guild_b_role_id, label FROM "
                     "role_sync_map ORDER BY rowid",
                     -1, &stmt, NULL);

  char *out = malloc(1);
  out[0] = '\0';
  size_t out_len = 0;
  int found = 0;

  const char *header = "role sync mappings:\n";
  out = realloc(out, out_len + strlen(header) + 1);
  strcpy(out + out_len, header);
  out_len += strlen(header);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    char line[128];
    u64snowflake a = (u64snowflake)sqlite3_column_int64(stmt, 0);
    u64snowflake b = (u64snowflake)sqlite3_column_int64(stmt, 1);
    const char *label = (const char *)sqlite3_column_text(stmt, 2);
    int n = snprintf(line, sizeof line, "%" PRIu64 " -> %" PRIu64 "%s%s\n", a,
                     b, label ? " : " : "", label ? label : "");

    out = realloc(out, out_len + n + 1);
    memcpy(out + out_len, line, n + 1);
    out_len += n;
    found = 1;
  }
  sqlite3_finalize(stmt);

  if (!found) {
    struct discord_create_message reply = {.content =
                                               "no role mappings configured"};
    discord_create_message(client, event->channel_id, &reply, NULL);
  } else {
    rolesync_send_chunked(client, event->channel_id, out);
  }
  free(out);
}
static const struct {
  const char *name;
  void (*fn)(struct discord *, const struct discord_message *, const char *);
} rolesync_subcommands[] = {
    {"map", &rolesync_map},
    {"unmap", &rolesync_unmap},
    {"list", &rolesync_list},
    {"generate", &rolesync_generate},
};

void rolesync_command(struct discord *client,
                      const struct discord_message *event) {
  if (!event || !event->author || event->author->bot)
    return;

  /*
   * Role-sync mappings are shared by the two configured guilds. Do not allow
   * commands from DMs or unrelated guilds to change that shared configuration.
   */
  if (event->guild_id != GUILD_A_ID && event->guild_id != GUILD_B_ID)
    return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL,
                              1549499601093009449ULL};
  if (!check_perm_byrole(&ctx, allowlist,
                         (int)(sizeof allowlist / sizeof allowlist[0]))) {
    struct discord_create_message reply = {
        .content = "You do not have permission to manage role sync."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  char sub[16] = "";
  int n = 0;
  sscanf(event->content, " %15s%n", sub, &n);
  const char *rest = event->content + n;

  for (size_t i = 0;
       i < sizeof(rolesync_subcommands) / sizeof(*rolesync_subcommands); i++) {
    if (strcmp(sub, rolesync_subcommands[i].name) == 0) {
      rolesync_subcommands[i].fn(client, event, rest);
      return;
    }
  }
  struct discord_create_message reply = {
      .content = "usage: +rolesync map|unmap|list ..."};
  discord_create_message(client, event->channel_id, &reply, NULL);
}

static void on_rolesync_fail(struct discord *client,
                             struct discord_response *resp) {
  (void)client;
  fprintf(stderr, "rolesync: add_guild_member_role failed, code=%d\n",
          resp->code);
}
typedef struct {
  u64snowflake user_id;
} rolesync_lookup_context;

static void rolesync_lookup_context_cleanup(struct discord *client,
                                            void *data) {
  (void)client;
  free(data);
}

static void on_rolesync_member_lookup_fail(struct discord *client,
                                           struct discord_response *resp) {
  const rolesync_lookup_context *ctx = resp ? resp->data : NULL;
  if (ctx) {
    log_info("rolesync: source-guild lookup failed for user %" PRIu64
             " (Discord code %d)",
             ctx->user_id, resp->code);
  }
  (void)client;
}

static void on_rolesync_member_lookup_done(
    struct discord *client, struct discord_response *resp,
    const struct discord_guild_member *member_a) {
  const rolesync_lookup_context *ctx = resp ? resp->data : NULL;
  if (!ctx || !ctx->user_id || !member_a || !member_a->roles)
    return;

  const u64snowflake uid = ctx->user_id;
  struct discord_roles target_roles = {0};
  struct discord_ret_roles target_ret = {.sync = &target_roles};
  if (discord_get_guild_roles(client, GUILD_B_ID, &target_ret) != CCORD_OK) {
    log_error("rolesync: unable to refresh target roles; no grants issued for user %"
              PRIu64, uid);
    return;
  }

  sqlite3 *db = customcom_get_db();
  int queued = 0;

  for (int i = 0; i < member_a->roles->size; i++) {
    u64snowflake a_role = member_a->roles->array[i];
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(
            db,
            "SELECT guild_b_role_id FROM role_sync_map WHERE guild_a_role_id = ?",
            -1, &stmt, NULL) != SQLITE_OK) {
      log_error("rolesync: failed to prepare role mapping lookup");
      continue;
    }
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)a_role);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      u64snowflake b_role = (u64snowflake)sqlite3_column_int64(stmt, 0);
      sqlite3_finalize(stmt);

      bool target_found = false, target_is_admin = false;
      for (int j = 0; j < target_roles.size; ++j) {
        if (target_roles.array[j].id == b_role) {
          target_found = true;
          target_is_admin =
              (target_roles.array[j].permissions &
               DISCORD_PERM_ADMINISTRATOR) != 0;
          break;
        }
      }
      if (!target_found || target_is_admin) {
        log_error("rolesync: refusing stale or Administrator target role %"
                  PRIu64 " for user %" PRIu64, b_role, uid);
        continue;
      }

      struct discord_add_guild_member_role params = {.reason = "rolesync"};
      struct discord_ret ret = {.fail = &on_rolesync_fail};
      CCORDcode status =
          discord_add_guild_member_role(client, GUILD_B_ID, uid, b_role,
                                        &params, &ret);
      if (status == CCORD_PENDING)
        queued++;
      else
        log_error("rolesync: failed to queue role grant %" PRIu64
                  " for user %" PRIu64 " (code %d)",
                  b_role, uid, status);
    } else {
      sqlite3_finalize(stmt);
    }
  }

  log_info("rolesync: queued %d role grant(s) for user %" PRIu64
           " in guild B",
           queued, uid);
}

void on_guild_member_add_rolesync(struct discord *client,
                                  const struct discord_guild_member *event) {
  if (!event || !event->guild_id || event->guild_id != GUILD_B_ID ||
      !event->user)
    return;

  rolesync_lookup_context *ctx = calloc(1, sizeof *ctx);
  if (!ctx) {
    log_error("rolesync: unable to allocate lookup context for user %" PRIu64,
              event->user->id);
    return;
  }
  ctx->user_id = event->user->id;

  struct discord_ret_guild_member ret = {
      .data = ctx,
      .cleanup = &rolesync_lookup_context_cleanup,
      .done = &on_rolesync_member_lookup_done,
      .fail = &on_rolesync_member_lookup_fail,
  };
  CCORDcode status =
      discord_get_guild_member(client, GUILD_A_ID, ctx->user_id, &ret);
  if (status != CCORD_PENDING) {
    log_error("rolesync: failed to queue source-guild member lookup for user %"
              PRIu64 " (code %d)",
              ctx->user_id, status);
    free(ctx);
  }
}

static void rolesync_generate(struct discord *client,
                              const struct discord_message *event,
                              const char *args) {
  (void)args;

  struct discord_roles roles_a = {0}, roles_b = {0};
  struct discord_ret_roles ret_a = {.sync = &roles_a};
  struct discord_ret_roles ret_b = {.sync = &roles_b};

  if (discord_get_guild_roles(client, GUILD_A_ID, &ret_a) != CCORD_OK) {
    struct discord_create_message reply = {
        .content = "Failed to fetch source guild roles."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }
  if (discord_get_guild_roles(client, GUILD_B_ID, &ret_b) != CCORD_OK) {
    struct discord_create_message reply = {
        .content = "Failed to fetch target guild roles."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  sqlite3 *db = customcom_get_db();
  char *matched = malloc(1);
  matched[0] = '\0';
  size_t matched_len = 0;
  char *unmatched = malloc(1);
  unmatched[0] = '\0';
  size_t unmatched_len = 0;
  bool any_matched = false, any_unmatched = false;

  for (int i = 0; i < roles_a.size; i++) {
    u64snowflake a_id = roles_a.array[i].id;
    const char *a_name = roles_a.array[i].name;

    if (a_id == GUILD_A_ID)
      continue;

    u64snowflake b_id = 0;
    bool found = false;
    for (int j = 0; j < roles_b.size; j++) {
      if (strcmp(roles_b.array[j].name, a_name) == 0 &&
          !(roles_b.array[j].permissions & DISCORD_PERM_ADMINISTRATOR)) {
        b_id = roles_b.array[j].id;
        found = true;
        break;
      }
    }

    if (found) {
      sqlite3_stmt *stmt;
      sqlite3_prepare_v2(
          db,
          "INSERT OR IGNORE INTO role_sync_map (guild_a_role_id, "
          "guild_b_role_id, label) VALUES (?, ?, ?)",
          -1, &stmt, NULL);
      sqlite3_bind_int64(stmt, 1, (sqlite3_int64)a_id);
      sqlite3_bind_int64(stmt, 2, (sqlite3_int64)b_id);
      sqlite3_bind_text(stmt, 3, a_name, -1, SQLITE_STATIC);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);

      char line[128];
      int n = snprintf(line, sizeof line, "%s\n", a_name);
      matched = realloc(matched, matched_len + n + 1);
      memcpy(matched + matched_len, line, n + 1);
      matched_len += n;
      any_matched = true;
    } else {
      char line[128];
      int n = snprintf(line, sizeof line, "%s\n", a_name);
      unmatched = realloc(unmatched, unmatched_len + n + 1);
      memcpy(unmatched + unmatched_len, line, n + 1);
      unmatched_len += n;
      any_unmatched = true;
    }
  }

  char *report = malloc(matched_len + unmatched_len + 64);
  sprintf(report, "**synced:**\n%s\n**no match found:**\n%s",
          any_matched ? matched : "none\n",
          any_unmatched ? unmatched : "none\n");

  rolesync_send_chunked(client, event->channel_id, report);

  free(matched);
  free(unmatched);
  free(report);
}
