#ifndef CUSTOMCOM_COOLDOWN_H
#define CUSTOMCOM_COOLDOWN_H

#include <stdint.h>
#include <sqlite3.h>

typedef enum {
  CUSTOMCOM_COOLDOWN_ERROR = -1,
  CUSTOMCOM_COOLDOWN_ACTIVE = 0,
  CUSTOMCOM_COOLDOWN_ACQUIRED = 1
} customcom_cooldown_result;

/* Atomically acquire a persistent per-user command cooldown. */
customcom_cooldown_result customcom_cooldown_try_acquire(
    sqlite3 *db, uint64_t user_id, const char *command_name,
    int cooldown_seconds, int64_t now, int64_t *remaining_seconds);

#endif
