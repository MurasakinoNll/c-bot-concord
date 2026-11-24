#include "core/ticketsystem.h"
#include "customcom/customcom.h"
#include "utils/utils.h"
#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  u64snowflake owner_id;
  int ticket_num;
  char owner_username[64];
} ticket_row;

#define SWEEP_MAX_ITEMS 256

typedef struct {
  u64snowflake channel_id;
  int ticket_num;
  char owner_username[64];
} sweep_item;

typedef struct {
  sweep_item items[SWEEP_MAX_ITEMS];
  int count;
  int index;
  int closed_count;
  int failed_count;
  int pending_count;
  u64snowflake report_channel_id;
  unsigned timer_id;
} sweep_state;

static sweep_state g_sweep = {0};

typedef struct {
  u64snowflake channel_id;
  u64snowflake report_channel_id;
  sweep_state *sweep;
} ticket_close_context;

static bool ticket_close_channel(struct discord *client,
                                 u64snowflake channel_id,
                                 const char *owner_username, int ticket_num,
                                 u64snowflake report_channel_id,
                                 sweep_state *sweep);

void ticket_db_init(void) {
  sqlite3 *db = customcom_get_db();
  sqlite3_exec(db,
               "CREATE TABLE IF NOT EXISTS tickets ("
               "channel_id INTEGER PRIMARY KEY, owner_id INTEGER NOT NULL, "
               "ticket_num INTEGER, status TEXT DEFAULT 'open');",
               NULL, NULL, NULL);
  sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN owner_username TEXT;", NULL,
               NULL, NULL);
  sqlite3_exec(
      db,
      "CREATE TRIGGER IF NOT EXISTS tickets_one_open_per_owner_insert "
      "BEFORE INSERT ON tickets WHEN NEW.status = 'open' AND EXISTS ("
      "SELECT 1 FROM tickets WHERE owner_id = NEW.owner_id AND status = 'open') "
      "BEGIN SELECT RAISE(ABORT, 'owner already has an open ticket'); END;",
      NULL, NULL, NULL);
  sqlite3_exec(
      db,
      "CREATE TRIGGER IF NOT EXISTS tickets_one_open_per_owner_update "
      "BEFORE UPDATE OF owner_id, status ON tickets "
      "WHEN NEW.status = 'open' AND EXISTS ("
      "SELECT 1 FROM tickets WHERE owner_id = NEW.owner_id AND status = 'open' "
      "AND channel_id <> OLD.channel_id) "
      "BEGIN SELECT RAISE(ABORT, 'owner already has an open ticket'); END;",
      NULL, NULL, NULL);
}

static u64snowflake ticket_find_open(u64snowflake owner_id) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt = NULL;
  u64snowflake found = 0;

  if (sqlite3_prepare_v2(
          db,
          "SELECT channel_id FROM tickets WHERE owner_id = ? AND status = 'open'",
          -1, &stmt, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)owner_id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    found = (u64snowflake)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return found;
}

static bool ticket_insert(u64snowflake channel_id, u64snowflake owner_id,
                          int ticket_num, const char *username) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(
          db,
          "INSERT INTO tickets (channel_id, owner_id, ticket_num, "
          "owner_username, status) VALUES (?, ?, ?, ?, 'open')",
          -1, &stmt, NULL) != SQLITE_OK) {
    fprintf(stderr, "ticket_insert prepare failed: %s\n", sqlite3_errmsg(db));
    return false;
  }
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)channel_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)owner_id);
  sqlite3_bind_int(stmt, 3, ticket_num);
  sqlite3_bind_text(stmt, 4, username, -1, SQLITE_STATIC);
  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    fprintf(stderr, "ticket_insert FAILED: %s (rc=%d)\n", sqlite3_errmsg(db),
            rc);
  } else {
    fprintf(stderr, "ticket_insert OK: channel_id=%" PRIu64 "\n", channel_id);
  }
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

void channel_create(struct discord *client,
                    const struct discord_interaction *event) {
  if (!event->member || !event->member->user ||
      !event->member->user->username) {
    log_error(
        "Interaction event user profile is missing data structure profiles.");
    return;
  }

  u64snowflake owner_id = event->member->user->id;

  u64snowflake existing = ticket_find_open(owner_id);
  if (existing) {
    char buf[100];
    snprintf(buf, sizeof buf, "You already have an open ticket: <#%" PRIu64 ">",
             existing);
    struct discord_interaction_response resp = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data =
            &(struct discord_interaction_callback_data){
                .content = buf,
                .flags = DISCORD_MESSAGE_EPHEMERAL,
            },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp,
                                        NULL);
    return;
  }

  int tc = load_ticketcounter();
  char name_buffer[256] = {0};
  snprintf(name_buffer, sizeof(name_buffer), "Ticket-%s-%i",
           event->member->user->username, tc);
  snprintf(name_buffer, sizeof(name_buffer), "%s-%i-⚠️",
           event->member->user->username, tc);
  struct discord_overwrite overwrites[] = {
      {.id = 1155152569040130101ULL,
       .type = 0,
       .allow = 0ULL,
       .deny = 1ULL << 10},
      {.id = 1155152569526669391ULL,
       .type = 0,
       .allow = (1ULL << 10) | (1ULL << 11),
       .deny = 0ULL},
      {.id = owner_id,
       .type = 1,
       .allow = (1ULL << 10) | (1ULL << 11) | (1ULL << 16),
       .deny = 0ULL}};

  struct discord_create_guild_channel params = {
      .name = name_buffer,
      .type = 0,
      .parent_id = 1536042058903720058ULL,
      .permission_overwrites =
          &(struct discord_overwrites){.array = overwrites, .size = 3}};
  struct discord_channel new_channel = {0};
  struct discord_ret_channel ret = {.sync = &new_channel};

  CCORDcode channel_status =
      discord_create_guild_channel(client, event->guild_id, &params, &ret);

  if (channel_status != CCORD_OK) {
    log_error("guild channel generation failed with error code: %d",
              channel_status);
    struct discord_interaction_response resp = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data =
            &(struct discord_interaction_callback_data){
                .content = "something went wrong creating your ticket. Try "
                           "again shortly.",
                .flags = DISCORD_MESSAGE_EPHEMERAL,
            },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp,
                                        NULL);
    return;
  }

  if (!ticket_insert(new_channel.id, owner_id, tc,
                     event->member->user->username)) {
    char error[160];
    snprintf(error, sizeof error,
             "Your ticket channel was created, but its record could not be saved. "
             "Please contact staff and reference channel <#%llu>.",
             (unsigned long long)new_channel.id);
    struct discord_interaction_response failed = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &(struct discord_interaction_callback_data){
            .content = error,
            .flags = DISCORD_MESSAGE_EPHEMERAL,
        },
    };
    discord_create_interaction_response(client, event->id, event->token,
                                        &failed, NULL);
    log_error("ticket record insert failed; orphan channel=%" PRIu64,
              new_channel.id);
    return;
  }

  struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
      .data =
          &(struct discord_interaction_callback_data){
              .content = "Your ticket has been created :white_check_mark:",
              .flags = DISCORD_MESSAGE_EPHEMERAL,
          },
  };
  discord_create_interaction_response(client, event->id, event->token, &resp,
                                      NULL);

  welcome_message(client, new_channel.id);
  log_info("ticket channel successfully deployed via layout engine.");
}

static bool ticket_find_by_channel(u64snowflake channel_id, ticket_row *out) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt = NULL;
  bool found = false;

  fprintf(stderr, "ticket_find_by_channel looking for channel_id=%" PRIu64 "\n",
          channel_id);

  if (sqlite3_prepare_v2(
          db,
          "SELECT owner_id, ticket_num, owner_username FROM tickets "
          "WHERE channel_id = ? AND status = 'open'",
          -1, &stmt, NULL) != SQLITE_OK)
    return false;
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)channel_id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    out->owner_id = (u64snowflake)sqlite3_column_int64(stmt, 0);
    out->ticket_num = sqlite3_column_int(stmt, 1);
    const unsigned char *username = sqlite3_column_text(stmt, 2);
    snprintf(out->owner_username, sizeof out->owner_username, "%s",
             username ? (const char *)username : "unknown");
    found = true;
  }
  sqlite3_finalize(stmt);
  fprintf(stderr, "ticket_find_by_channel found=%d\n", found);
  return found;
}

void close_ticket(struct discord *client, const struct discord_message *event) {
  if (event->author->bot)
    return;
  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL};
  if (!check_perm_byrole(&ctx, allowlist, 1)) {
    fprintf(stderr, "ban rejected, invalid permissions\n");
    return;
  }

  ticket_row row;
  if (!ticket_find_by_channel(event->channel_id, &row)) {
    struct discord_create_message reply = {
        .content = "This isn't an open ticket channel."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }
  ticket_close_channel(client, event->channel_id, row.owner_username,
                       row.ticket_num, event->channel_id, NULL);
}
#define TICKET_GUILD_ID 1155152569040130101ULL
#define TICKET_ARCHIVE_CATEGORY 1552818898947145768ULL

static bool ticket_mark_closed(u64snowflake channel_id) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(
          db, "UPDATE tickets SET status = 'closed' WHERE channel_id = ?",
          -1, &stmt, NULL) != SQLITE_OK)
    return false;
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)channel_id);
  int rc = sqlite3_step(stmt);
  int changed = sqlite3_changes(db);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE && changed > 0;
}

static void ticket_close_finish(struct discord *client,
                                ticket_close_context *ctx,
                                bool discord_succeeded, CCORDcode code) {
  bool db_succeeded = false;
  if (discord_succeeded) {
    db_succeeded = ticket_mark_closed(ctx->channel_id);
    if (!db_succeeded)
      log_error("ticket: Discord archived channel %llu, but DB update failed",
                (unsigned long long)ctx->channel_id);
  } else {
    log_error("ticket: Discord archive request failed for channel %llu: %d",
              (unsigned long long)ctx->channel_id, code);
  }

  bool succeeded = discord_succeeded && db_succeeded;
  if (ctx->sweep) {
    if (ctx->sweep->pending_count > 0)
      ctx->sweep->pending_count--;
    if (succeeded)
      ctx->sweep->closed_count++;
    else
      ctx->sweep->failed_count++;
  } else if (ctx->report_channel_id) {
    const char *content =
        succeeded
            ? "Ticket closed. \xE2\x9C\x85"
            : (discord_succeeded
                   ? "Discord archived the channel, but its ticket record could not be updated. Please contact staff."
                   : "The ticket could not be archived. Its database state was left open.");
    struct discord_create_message reply = {.content = (char *)content};
    discord_create_message(client, ctx->report_channel_id, &reply, NULL);
  }
}

static void ticket_close_done(struct discord *client,
                              struct discord_response *resp,
                              const struct discord_channel *channel) {
  (void)channel;
  ticket_close_finish(client, resp->data, true, CCORD_OK);
}

static void ticket_close_fail(struct discord *client,
                              struct discord_response *resp) {
  ticket_close_finish(client, resp->data, false, resp->code);
}

static void ticket_close_context_cleanup(struct discord *client, void *data) {
  (void)client;
  free(data);
}

/*
 * Concord returns CCORD_PENDING for queued REST requests. The done/fail
 * callbacks report Discord's actual HTTP outcome; CCORD_OK here would only
 * describe local synchronous completion, not an async request's future result.
 */
static bool ticket_close_channel(struct discord *client,
                                 u64snowflake channel_id,
                                 const char *owner_username, int ticket_num,
                                 u64snowflake report_channel_id,
                                 sweep_state *sweep) {
  ticket_close_context *ctx = calloc(1, sizeof *ctx);
  if (!ctx) {
    log_error("ticket: unable to allocate archive request context");
    if (sweep)
      sweep->failed_count++;
    else if (report_channel_id) {
      struct discord_create_message reply = {
          .content = "The ticket could not be archived. Please try again."};
      discord_create_message(client, report_channel_id, &reply, NULL);
    }
    return false;
  }

  ctx->channel_id = channel_id;
  ctx->report_channel_id = report_channel_id;
  ctx->sweep = sweep;
  if (sweep)
    sweep->pending_count++;

  char new_name[256];
  snprintf(new_name, sizeof new_name, "%s-%d-\xE2\x9C\x85", owner_username,
           ticket_num);
  struct discord_modify_channel params = {
      .name = new_name,
      .parent_id = TICKET_ARCHIVE_CATEGORY,
  };
  struct discord_ret_channel ret = {
      .data = ctx,
      .cleanup = &ticket_close_context_cleanup,
      .done = &ticket_close_done,
      .fail = &ticket_close_fail,
  };

  CCORDcode status = discord_modify_channel(client, channel_id, &params, &ret);
  if (status != CCORD_PENDING) {
    ticket_close_finish(client, ctx, false, status);
    free(ctx);
    return false;
  }
  return true;
}

void on_guild_member_remove_ticket_close(
    struct discord *client, const struct discord_guild_member_remove *event) {
  if (event->guild_id != TICKET_GUILD_ID)
    return;
  if (!event->user)
    return;

  u64snowflake uid = event->user->id;
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;

  sweep_item pending[SWEEP_MAX_ITEMS];
  int pending_count = 0;
  if (sqlite3_prepare_v2(
          db,
          "SELECT channel_id, ticket_num, owner_username FROM tickets "
          "WHERE owner_id = ? AND status = 'open'",
          -1, &stmt, NULL) != SQLITE_OK) {
    log_error("ticket: failed to load open tickets for departing user %"
              PRIu64,
              uid);
    return;
  }
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)uid);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (pending_count == SWEEP_MAX_ITEMS) {
      log_error("ticket: more than %d open tickets for user %" PRIu64
                "; remaining channels were not queued",
                SWEEP_MAX_ITEMS, uid);
      break;
    }
    sweep_item *item = &pending[pending_count++];
    item->channel_id = (u64snowflake)sqlite3_column_int64(stmt, 0);
    item->ticket_num = sqlite3_column_int(stmt, 1);
    const unsigned char *username = sqlite3_column_text(stmt, 2);
    snprintf(item->owner_username, sizeof item->owner_username, "%s",
             username ? (const char *)username : "unknown");
  }
  sqlite3_finalize(stmt);

  /* Submit only after closing the read cursor: REST callbacks may write SQLite. */
  for (int i = 0; i < pending_count; i++) {
    sweep_item *item = &pending[i];
    if (ticket_close_channel(client, item->channel_id, item->owner_username,
                             item->ticket_num, 0, NULL))
      log_info("ticket: archive request queued for channel %" PRIu64
               " after owner %" PRIu64 " left/kicked/banned",
               item->channel_id, uid);
  }
}

static void sweep_tick(struct discord *client, struct discord_timer *timer) {
  sweep_state *state = (sweep_state *)timer->data;

  /* Keep at most one archive request in flight during a sweep. */
  if (state->pending_count > 0)
    return;

  if (state->index >= state->count) {
    char buf[64];
    snprintf(buf, sizeof buf,
             "Ticket sweep complete: %d closed, %d failed.",
             state->closed_count, state->failed_count);
    struct discord_create_message reply = {.content = buf};
    discord_create_message(client, state->report_channel_id, &reply, NULL);
    discord_timer_cancel_and_delete(client, state->timer_id);
    state->timer_id = 0;
    return;
  }

  sweep_item *item = &state->items[state->index++];
  ticket_close_channel(client, item->channel_id, item->owner_username,
                       item->ticket_num, 0, state);
}

void ticketsweep_command(struct discord *client,
                         const struct discord_message *event) {
  if (event->author->bot)
    return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL};
  if (!check_perm_byrole(&ctx, allowlist, 1)) {
    fprintf(stderr, "ticketsweep rejected, invalid permissions\n");
    return;
  }

  if (g_sweep.timer_id) {
    struct discord_create_message reply = {
        .content = "A ticket sweep is already running."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(
          db,
          "SELECT channel_id, ticket_num, owner_username FROM "
          "tickets WHERE status = 'open'",
          -1, &stmt, NULL) != SQLITE_OK) {
    log_error("ticketsweep: failed to load open tickets: %s", sqlite3_errmsg(db));
    return;
  }

  g_sweep.count = 0;
  while (g_sweep.count < SWEEP_MAX_ITEMS && sqlite3_step(stmt) == SQLITE_ROW) {
    sweep_item *item = &g_sweep.items[g_sweep.count];
    item->channel_id = (u64snowflake)sqlite3_column_int64(stmt, 0);
    item->ticket_num = sqlite3_column_int(stmt, 1);
    strncpy(item->owner_username, (const char *)sqlite3_column_text(stmt, 2),
            sizeof(item->owner_username) - 1);
    g_sweep.count++;
  }
  sqlite3_finalize(stmt);

  if (g_sweep.count == 0) {
    struct discord_create_message reply = {.content =
                                               "No open tickets to sweep."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  g_sweep.index = 0;
  g_sweep.closed_count = 0;
  g_sweep.failed_count = 0;
  g_sweep.pending_count = 0;
  g_sweep.report_channel_id = event->channel_id;
  g_sweep.timer_id =
      discord_timer_interval(client, &sweep_tick, NULL, &g_sweep, 0, 750, -1);

  char buf[64];
  snprintf(buf, sizeof buf, "sweeping %d open ticket(s)...", g_sweep.count);
  struct discord_create_message reply = {.content = buf};
  discord_create_message(client, event->channel_id, &reply, NULL);
}
