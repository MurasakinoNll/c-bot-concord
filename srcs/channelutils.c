#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include "customcom.h"
#include "ticketsystem.h"

void ticket_db_init(void){
  sqlite3 *db = customcom_get_db();
  sqlite3_exec(db,
               "CREATE TABLE IF NOT EXSITS tickets ("
               "channel_id INTEGER PRIMARY KEY, owner_id INTEGER NOT NULL, "
               "ticket_num INTEGER, status TEXT DEFAULT 'open');",
               NULL, NULL, NULL);
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
static void ticket_insert(u64snowflake channel_id, u64snowflake owner_id, int ticket_num) {
  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db,
    "INSERT INTO tickets (channel_id, owner_id, ticket_num, status) VALUES (?, ?, ?, 'open')",
    -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)channel_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)owner_id);
  sqlite3_bind_int(stmt, 3, ticket_num);
  sqlite3_step(stmt);
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

    ticket_insert(new_channel.id, owner_id, tc);

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
