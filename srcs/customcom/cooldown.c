#include "customcom/cooldown.h"

#include <limits.h>
#include <stddef.h>

customcom_cooldown_result customcom_cooldown_try_acquire(
    sqlite3 *db, uint64_t user_id, const char *command_name,
    int cooldown_seconds, int64_t now, int64_t *remaining_seconds) {
  if (!db || !command_name || !*command_name || !remaining_seconds ||
      user_id == 0 || user_id > (uint64_t)INT64_MAX ||
      cooldown_seconds <= 0 || now < 0)
    return CUSTOMCOM_COOLDOWN_ERROR;

  *remaining_seconds = 0;
  sqlite3_stmt *stmt = NULL;
  const char *upsert =
      "INSERT INTO customcom_cooldowns (user_id, command_name, last_used) "
      "VALUES (?, ?, ?) ON CONFLICT(user_id, command_name) "
      "DO UPDATE SET last_used = excluded.last_used "
      "WHERE excluded.last_used >= customcom_cooldowns.last_used "
      "AND excluded.last_used - customcom_cooldowns.last_used >= ?";

  if (sqlite3_prepare_v2(db, upsert, -1, &stmt, NULL) != SQLITE_OK)
    return CUSTOMCOM_COOLDOWN_ERROR;

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)user_id);
  sqlite3_bind_text(stmt, 2, command_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now);
  sqlite3_bind_int(stmt, 4, cooldown_seconds);
  int rc = sqlite3_step(stmt);
  int acquired = sqlite3_changes(db);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
    return CUSTOMCOM_COOLDOWN_ERROR;
  if (acquired > 0)
    return CUSTOMCOM_COOLDOWN_ACQUIRED;

  const char *select_last_used =
      "SELECT last_used FROM customcom_cooldowns "
      "WHERE user_id = ? AND command_name = ?";
  if (sqlite3_prepare_v2(db, select_last_used, -1, &stmt, NULL) != SQLITE_OK)
    return CUSTOMCOM_COOLDOWN_ERROR;
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)user_id);
  sqlite3_bind_text(stmt, 2, command_name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return CUSTOMCOM_COOLDOWN_ERROR;
  }

  sqlite3_int64 last_used = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);

  /* A negative or future timestamp is treated conservatively as a full wait. */
  int64_t elapsed = (last_used >= 0 && now >= last_used) ? now - last_used : 0;
  int64_t remaining = (int64_t)cooldown_seconds - elapsed;
  *remaining_seconds = remaining > 0 ? remaining : 1;
  return CUSTOMCOM_COOLDOWN_ACTIVE;
}
