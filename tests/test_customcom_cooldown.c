#include "customcom/cooldown.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
  sqlite3 *db = NULL;
  assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
  assert(sqlite3_exec(
             db,
             "CREATE TABLE customcom_cooldowns ("
             "user_id INTEGER NOT NULL, command_name TEXT NOT NULL, "
             "last_used INTEGER NOT NULL, "
             "PRIMARY KEY(user_id, command_name));",
             NULL, NULL, NULL) == SQLITE_OK);

  int64_t remaining = -1;
  assert(customcom_cooldown_try_acquire(db, 10, "ping", 10, 100,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ACQUIRED);
  assert(remaining == 0);

  assert(customcom_cooldown_try_acquire(db, 10, "ping", 10, 105,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ACTIVE);
  assert(remaining == 5);

  assert(customcom_cooldown_try_acquire(db, 10, "ping", 10, 110,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ACQUIRED);
  assert(remaining == 0);

  /* Clock rollback must not bypass the cooldown. */
  assert(customcom_cooldown_try_acquire(db, 10, "ping", 10, 109,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ACTIVE);
  assert(remaining == 10);

  /* Cooldowns are isolated by user and command. */
  assert(customcom_cooldown_try_acquire(db, 11, "ping", 10, 105,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ACQUIRED);
  assert(customcom_cooldown_try_acquire(db, 10, "other", 10, 105,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ACQUIRED);

  assert(customcom_cooldown_try_acquire(db, UINT64_MAX, "bad", 10, 105,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ERROR);
  assert(customcom_cooldown_try_acquire(db, 10, "ping", 0, 105,
                                        &remaining) ==
         CUSTOMCOM_COOLDOWN_ERROR);

  assert(sqlite3_close(db) == SQLITE_OK);
  puts("custom-command cooldown tests: PASS");
  return 0;
}
