#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "customcom.h"
#include "ticketsystem.h"
#include "utils.h"
typedef struct {
  u64snowflake owner_id;
  int ticket_num;
  char owner_username[64];
} ticket_row;

void ticket_db_init(void){
  sqlite3 *db = customcom_get_db();
  sqlite3_exec(db,
               "CREATE TABLE IF NOT EXISTS tickets ("
               "channel_id INTEGER PRIMARY KEY, owner_id INTEGER NOT NULL, "
               "ticket_num INTEGER, status TEXT DEFAULT 'open');",
               NULL, NULL, NULL);
  sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN owner_username TEXT;", NULL, NULL, NULL);
}

static u64snowflake ticket_find_open(u64snowflake owner_id) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  u64snowflake found = 0;

  sqlite3_prepare_v2(db, "SELECT channel_id FROM tickets WHERE owner_id = ? AND status = 'open'",
                     -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)owner_id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    found = (u64snowflake)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return found;
}

static void ticket_insert(u64snowflake channel_id, u64snowflake owner_id, int ticket_num, const char *username) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db,
    "INSERT INTO tickets (channel_id, owner_id, ticket_num, owner_username, status) VALUES (?, ?, ?, ?, 'open')",
    -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)channel_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)owner_id);
  sqlite3_bind_int(stmt, 3, ticket_num);
  sqlite3_bind_text(stmt, 4, username, -1, SQLITE_STATIC);
  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    fprintf(stderr, "ticket_insert FAILED: %s (rc=%d)\n", sqlite3_errmsg(db), rc);
  } else {
    fprintf(stderr, "ticket_insert OK: channel_id=%" PRIu64 "\n", channel_id);
  }
  sqlite3_finalize(stmt);
}

void channel_create(struct discord *client, const struct discord_interaction *event){
    if (!event->member || !event->member->user || !event->member->user->username) {
        log_error("Interaction event user profile is missing data structure profiles.");
        return;
    }

    u64snowflake owner_id = event->member->user->id;

    u64snowflake existing = ticket_find_open(owner_id);
    if (existing) {
        char buf[100];
        snprintf(buf, sizeof buf, "You already have an open ticket: <#%" PRIu64 ">", existing);
        struct discord_interaction_response resp = {
            .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
            .data = &(struct discord_interaction_callback_data){
                .content = buf,
                .flags = DISCORD_MESSAGE_EPHEMERAL,
            },
        };
        discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
        return;
    }

    int tc = load_ticketcounter();
    char name_buffer[256] = {0};
    snprintf(name_buffer, sizeof(name_buffer), "Ticket-%s-%i", event->member->user->username, tc);
    snprintf(name_buffer, sizeof(name_buffer), "%s-%i-⚠️", event->member->user->username, tc);
    struct discord_overwrite overwrites[] = {
        { .id = 1155152569040130101ULL, .type = 0, .allow = 0ULL, .deny = 1ULL << 10 },
        { .id = 1155152569526669391ULL, .type = 0, .allow = (1ULL << 10) | (1ULL << 11), .deny = 0ULL },
        { .id = owner_id, .type = 1, .allow = (1ULL << 10) | (1ULL << 11) | (1ULL << 16), .deny = 0ULL }
    };

    struct discord_create_guild_channel params = {
        .name = name_buffer,
        .type = 0,
        .parent_id = 1536042058903720058ULL,
        .permission_overwrites = &(struct discord_overwrites){ .array = overwrites, .size = 3 }
    };
    struct discord_channel new_channel = {0};
    struct discord_ret_channel ret = { .sync = &new_channel };

    CCORDcode channel_status = discord_create_guild_channel(client, event->guild_id, &params, &ret);

    if (channel_status != CCORD_OK) {
        log_error("guild channel generation failed with error code: %d", channel_status);
        struct discord_interaction_response resp = {
            .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
            .data = &(struct discord_interaction_callback_data){
                .content = "something went wrong creating your ticket. Try again shortly.",
                .flags = DISCORD_MESSAGE_EPHEMERAL,
            },
        };
        discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
        return;
    }

    ticket_insert(new_channel.id, owner_id, tc, event->member->user->username);

    struct discord_interaction_response resp = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &(struct discord_interaction_callback_data){
            .content = "Your ticket has been created :white_check_mark:",
            .flags = DISCORD_MESSAGE_EPHEMERAL,
        },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp, NULL);

    welcome_message(client, new_channel.id);
    log_info("ticket channel successfully deployed via layout engine.");
}

static bool ticket_find_by_channel(u64snowflake channel_id, ticket_row *out) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  bool found = false;

  fprintf(stderr, "ticket_find_by_channel looking for channel_id=%" PRIu64 "\n", channel_id);

  sqlite3_prepare_v2(db,
    "SELECT owner_id, ticket_num, owner_username FROM tickets WHERE channel_id = ? AND status = 'open'",
    -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)channel_id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    out->owner_id = (u64snowflake)sqlite3_column_int64(stmt, 0);
    out->ticket_num = sqlite3_column_int(stmt, 1);
    strncpy(out->owner_username, (const char*)sqlite3_column_text(stmt, 2), sizeof(out->owner_username) - 1);
    found = true;
  }
  sqlite3_finalize(stmt);
  fprintf(stderr, "ticket_find_by_channel found=%d\n", found);
  return found;
}

void close_ticket(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;

  ticket_row row;
  if (!ticket_find_by_channel(event->channel_id, &row)) {
    struct discord_create_message reply = { .content = "this isnt an open ticket channel." };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  bool is_owner = (event->author->id == row.owner_id);
  bool is_mod = check_perm_byrole(&ctx, allowlist, 1);

  if (!is_owner && !is_mod) {
    struct discord_create_message reply = { .content = "You don't have permission to close this ticket." };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  char new_name[256];
  snprintf(new_name, sizeof new_name, "%s-%d-✅", row.owner_username, row.ticket_num);

  struct discord_modify_channel params = { .name = new_name };
  discord_modify_channel(client, event->channel_id, &params, NULL);

  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "UPDATE tickets SET status = 'closed' WHERE channel_id = ?", -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)event->channel_id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  struct discord_create_message reply = { .content = "Ticket closed. ✅" };
  discord_create_message(client, event->channel_id, &reply, NULL);
}
