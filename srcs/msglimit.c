#include "msglimit.h"
#include "customcom.h"
#include "utils.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void msglimit_db_init(void) {
  sqlite3 *db = customcom_get_db();
  sqlite3_exec(db,
    "CREATE TABLE IF NOT EXISTS msglimit_settings ("
    "guild_id INTEGER, user_id INTEGER, "
    "text_limit INTEGER DEFAULT 0, media_limit INTEGER DEFAULT 0, "
    "PRIMARY KEY(guild_id, user_id));",
    NULL, NULL, NULL);
  sqlite3_exec(db,
    "CREATE TABLE IF NOT EXISTS msglimit_counts ("
    "guild_id INTEGER, user_id INTEGER, day TEXT, "
    "text_count INTEGER DEFAULT 0, media_count INTEGER DEFAULT 0, "
    "PRIMARY KEY(guild_id, user_id, day));",
    NULL, NULL, NULL);
}

static void get_today_utc(char *buf, size_t sz) {
  time_t now = time(NULL);
  struct tm *tmv = gmtime(&now);
  strftime(buf, sz, "%Y-%m-%d", tmv);
}

static bool message_is_media(const struct discord_message *event) {
  if (event->attachments && event->attachments->size > 0) return true;
  if (event->content && (strstr(event->content, "http://") || strstr(event->content, "https://"))) return true;
  return false;
}

void msglimit_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)) {
    fprintf(stderr, "msglimit_command rejected, invalid permissions\n");
    return;
  }

  char type[16] = "";
  int n1 = 0;
  sscanf(event->content, " %15s%n", type, &n1);

  const char *col = NULL;
  if (strcmp(type, "text") == 0) col = "text_limit";
  else if (strcmp(type, "media") == 0) col = "media_limit";

  u64snowflake target_uid = 0;
  if (event->mentions && event->mentions->size > 0) {
    target_uid = event->mentions->array[0].id;
  }

  char uidtok[64] = "";
  int n2 = 0;
  sscanf(event->content + n1, " %63s%n", uidtok, &n2);
  if (!target_uid) {
    sscanf(uidtok, "%" SCNu64, &target_uid);
  }

  int limit = -1;
  sscanf(event->content + n1 + n2, " %d", &limit);

  if (!col || !target_uid || limit < 0) {
    struct discord_create_message reply = { .content = "usage: +msglimit text <uid|mention> <int> | +msglimit media <uid|mention> <int>" };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  char sql[256];
  snprintf(sql, sizeof sql,
    "INSERT INTO msglimit_settings (guild_id, user_id, %s) VALUES (?, ?, ?) "
    "ON CONFLICT(guild_id, user_id) DO UPDATE SET %s = excluded.%s", col, col, col);

  sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)event->guild_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)target_uid);
  sqlite3_bind_int(stmt, 3, limit);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  char buf[128];
  if (limit == 0) {
    snprintf(buf, sizeof buf, "msglimit %s disabled for <@%" PRIu64 ">.", type, target_uid);
  } else {
    snprintf(buf, sizeof buf, "msglimit %s for <@%" PRIu64 "> set to %d (resets 00:00 UTC).", type, target_uid, limit);
  }
  struct discord_create_message reply = { .content = buf };
  discord_create_message(client, event->channel_id, &reply, NULL);
}

bool msglimit_enforce(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return false;
  if (!event->guild_id) return false;

  sqlite3 *db = customcom_get_db();
  sqlite3_stmt *stmt;
  int text_limit = 0, media_limit = 0;

  sqlite3_prepare_v2(db,
    "SELECT text_limit, media_limit FROM msglimit_settings WHERE guild_id = ? AND user_id = ?",
    -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)event->guild_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)event->author->id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    text_limit = sqlite3_column_int(stmt, 0);
    media_limit = sqlite3_column_int(stmt, 1);
  }
  sqlite3_finalize(stmt);

  bool is_media = message_is_media(event);
  int limit = is_media ? media_limit : text_limit;
  if (limit <= 0) return false;
  char day[16];
  get_today_utc(day, sizeof day);
  const char *col = is_media ? "media_count" : "text_count";

  char sql[256];
  snprintf(sql, sizeof sql,
    "INSERT INTO msglimit_counts (guild_id, user_id, day, %s) VALUES (?, ?, ?, 1) "
    "ON CONFLICT(guild_id, user_id, day) DO UPDATE SET %s = %s + 1",
    col, col, col);
  sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)event->guild_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)event->author->id);
  sqlite3_bind_text(stmt, 3, day, -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  snprintf(sql, sizeof sql, "SELECT %s FROM msglimit_counts WHERE guild_id=? AND user_id=? AND day=?", col);
  sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)event->guild_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)event->author->id);
  sqlite3_bind_text(stmt, 3, day, -1, SQLITE_STATIC);
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (count > limit) {
    discord_delete_message(client, event->channel_id, event->id, NULL, NULL);
    return true;
  }
  return false;
}
