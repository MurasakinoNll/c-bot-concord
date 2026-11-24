#include <concord/discord.h>
#include <concord/log.h>
#include <sqlite3.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "customcom//customcom.h"
#include "customcom/cooldown.h"
#include "utils/utils.h"
static sqlite3 *db;
static const struct {
  const char *name;
  void (*fn)(struct discord*, const struct discord_message*, const char*);
} subcommands[] = {
  { "create",   &cc_create },
  { "delete",   &cc_delete },
  { "list",     &cc_search },
  { "search",   &cc_search },
  { "cooldown", &cc_cooldown },
};

typedef struct cc_node {
  char name[64];
  char response[400];
  int cooldown;
  struct cc_node *next;
} cc_node;

static void cc_cache_insert(const char *name, const char *response, int cooldown);
static void cc_cache_remove(const char *name);
static cc_node *cc_lookup(const char *name);
sqlite3 *customcom_get_db(void) { return db; }
void cc_dispatch(struct discord *client, const struct discord_message *event) {
  char sub[32] = "";
  int n = 0;
  sscanf(event->content, " %31s%n", sub, &n);
  const char *rest = event->content + n;

  for (size_t i = 0; i < sizeof(subcommands)/sizeof(*subcommands); i++) {
    if (strcmp(sub, subcommands[i].name) == 0) {
      subcommands[i].fn(client, event, rest);
      return;
    }
  }
  fprintf(stderr, "cc: unknown subcommand '%s'\n", sub);
}

void customcom_init(void) {
  int rc = sqlite3_open("customcoms.db", &db);
  if (rc != SQLITE_OK) {
    log_error("customcom: database open failed: %s",
              db ? sqlite3_errmsg(db) : "no database handle");
    if (db)
      sqlite3_close(db);
    db = NULL;
    return;
  }

  if (sqlite3_exec(
          db,
          "CREATE TABLE IF NOT EXISTS customcoms ("
          "name TEXT PRIMARY KEY, response TEXT NOT NULL, "
          "cooldown INTEGER DEFAULT 0, created_by TEXT, uses INTEGER DEFAULT 0);",
          NULL, NULL, NULL) != SQLITE_OK) {
    log_error("customcom: failed to initialize command table: %s",
              sqlite3_errmsg(db));
    return;
  }

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(
          db, "SELECT name, response, cooldown FROM customcoms", -1, &stmt,
          NULL) != SQLITE_OK) {
    log_error("customcom: failed to load command cache: %s", sqlite3_errmsg(db));
    return;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *name = sqlite3_column_text(stmt, 0);
    const unsigned char *response = sqlite3_column_text(stmt, 1);
    if (name && response)
      cc_cache_insert((const char *)name, (const char *)response,
                      sqlite3_column_int(stmt, 2));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    log_error("customcom: failed while loading command cache: %s",
              sqlite3_errmsg(db));
    return;
  }

  if (sqlite3_exec(
          db,
          "CREATE TABLE IF NOT EXISTS customcom_cooldowns ("
          "user_id INTEGER NOT NULL, command_name TEXT NOT NULL, "
          "last_used INTEGER NOT NULL, "
          "PRIMARY KEY(user_id, command_name));",
          NULL, NULL, NULL) != SQLITE_OK) {
    log_error("customcom: failed to initialize cooldown table: %s",
              sqlite3_errmsg(db));
  }
}

void cc_create(struct discord *client, const struct discord_message *event, const char *args) {
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "cc_create rejected, invalid permissions\n");
    return;
  }

  char name[64] = "", response[400] = "";
  sscanf(args, " %63s %399[^\n]", name, response);
  if (!*name || !*response) {
    fprintf(stderr, "cc create: usage <name> <response>\n");
    return;
  }

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db,
    "INSERT INTO customcoms (name, response, created_by) VALUES (?, ?, ?)",
    -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, response, -1, SQLITE_STATIC);
  char uidbuf[24]; snprintf(uidbuf, sizeof uidbuf, "%" PRIu64, event->author->id);
  sqlite3_bind_text(stmt, 3, uidbuf, -1, SQLITE_STATIC);

  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    fprintf(stderr, "cc_create failed: %s (rc=%d)\n", sqlite3_errmsg(db), rc);
  }
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE) {
    cc_cache_insert(name, response, 0);   // cache syncing refer to bottom of file
  }

  struct discord_create_message reply = {
    .content = (rc == SQLITE_DONE) ? "custom command created" : "cc already exists"
  };
  discord_create_message(client, event->channel_id, &reply, NULL);
}

void cc_delete(struct discord *client, const struct discord_message *event, const char *args) {
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "cc_delete rejected, invalid permissions\n");
    return;
  }

  char name[64] = "";
  sscanf(args, " %63s", name);
  if (!*name) {
    fprintf(stderr, "cc delete: usage <name>\n");
    return;
  }

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "DELETE FROM customcoms WHERE name = ?", -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  int changed = sqlite3_changes(db);
  sqlite3_finalize(stmt);

  if (changed > 0) {
    cc_cache_remove(name);   // cache syncing at bottom of file
  }

  struct discord_create_message reply = {
    .content = changed > 0 ? "custom command deleted" : "invalid cc name"
  };
  discord_create_message(client, event->channel_id, &reply, NULL);
}

#include "customcom/ccembed.h"// likely temporary

void cc_search(struct discord *client, const struct discord_message *event, const char *args) {
  char query[64] = "";
  sscanf(args, " %63s", query);
  ccembed_send_page(client, event->channel_id, event->author->id, query, 0);
}

static bool parse_nonnegative_int(const char *text, int *out) {
  if (!text || !out || *text < '0' || *text > '9')
    return false;
  int value = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9')
      return false;
    int digit = *p - '0';
    if (value > (INT_MAX - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  *out = value;
  return true;
}

static bool only_space_remains(const char *text) {
  if (!text)
    return false;
  while (*text == ' ' || *text == '\${ch}' || *text == '\${ch}' || *text == '\${ch}')
    ++text;
  return *text == '\${ch}';
}

void cc_cooldown(struct discord *client, const struct discord_message *event,
                 const char *args) {
  if (event->author->bot)
    return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {1155152569526669391ULL};
  if (!check_perm_byrole(&ctx, allowlist, 1)) {
    fprintf(stderr, "cc_cooldown rejected, invalid permissions\n");
    return;
  }

  char name[64] = "", seconds_token[32] = "";
  int first_end = 0, second_end = 0;
  int matched =
      sscanf(args, " %63s%n %31s%n", name, &first_end, seconds_token,
             &second_end);
  int seconds = 0;
  if (matched < 1 || !*name ||
      (matched == 1 && !only_space_remains(args + first_end)) ||
      (matched == 2 &&
       (!only_space_remains(args + second_end) ||
        !parse_nonnegative_int(seconds_token, &seconds)))) {
    fprintf(stderr, "cc cooldown: usage <name> [non-negative seconds]\n");
    return;
  }

  char buf[128];
  sqlite3_stmt *stmt = NULL;

  if (matched == 1) {
    if (sqlite3_prepare_v2(
            db, "SELECT cooldown FROM customcoms WHERE name = ?", -1, &stmt,
            NULL) != SQLITE_OK) {
      snprintf(buf, sizeof buf, "Database error reading cooldown.");
    } else {
      sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(buf, sizeof buf, "Cooldown for '%s': %ds", name,
                 sqlite3_column_int(stmt, 0));
      } else {
        snprintf(buf, sizeof buf, "No command named '%s'.", name);
      }
      sqlite3_finalize(stmt);
    }
  } else {
    if (sqlite3_prepare_v2(
            db, "UPDATE customcoms SET cooldown = ? WHERE name = ?", -1,
            &stmt, NULL) != SQLITE_OK) {
      snprintf(buf, sizeof buf, "Database error updating cooldown.");
    } else {
      sqlite3_bind_int(stmt, 1, seconds);
      sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
      int rc = sqlite3_step(stmt);
      int changed = sqlite3_changes(db);
      sqlite3_finalize(stmt);

      if (rc != SQLITE_DONE) {
        snprintf(buf, sizeof buf, "Database error updating cooldown.");
      } else if (changed > 0) {
        snprintf(buf, sizeof buf, "Cooldown for '%s' set to %ds.", name,
                 seconds);
        cc_node *cached = cc_lookup(name);
        if (cached)
          cached->cooldown = seconds;
      } else {
        snprintf(buf, sizeof buf, "No command named '%s'.", name);
      }
    }
  }

  struct discord_create_message reply = {.content = buf};
  discord_create_message(client, event->channel_id, &reply, NULL);
}
/* 
 *  IMPORTANT: this is very very experimental 99% chance it breaks
 *  if anything goes wrong its probably this
 * */

#include <stdlib.h>
#include <string.h>

#define CC_BUCKETS 128

static cc_node *cc_table[CC_BUCKETS];

static unsigned long djb2(const char *str) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++)) hash = ((hash << 5) + hash) + c;
  return hash;
}

static cc_node *cc_lookup(const char *name) {
  unsigned long h = djb2(name) % CC_BUCKETS;
  for (cc_node *n = cc_table[h]; n; n = n->next)
    if (strcmp(n->name, name) == 0) return n;
  return NULL;
}

static void cc_cache_insert(const char *name, const char *response, int cooldown) {
  if (!name || !response)
    return;
  cc_node *existing = cc_lookup(name);
  if (existing) {
    snprintf(existing->response, sizeof existing->response, "%s", response);
    existing->cooldown = cooldown;
    return;
  }
  cc_node *n = calloc(1, sizeof *n);
  if (!n) {
    fprintf(stderr, "cc: unable to allocate custom-command cache entry\n");
    return;
  }
  snprintf(n->name, sizeof n->name, "%s", name);
  snprintf(n->response, sizeof n->response, "%s", response);
  n->cooldown = cooldown;
  unsigned long h = djb2(name) % CC_BUCKETS;
  n->next = cc_table[h];
  cc_table[h] = n;
}

static void cc_cache_remove(const char *name) {
  unsigned long h = djb2(name) % CC_BUCKETS;
  cc_node **pp = &cc_table[h];
  while (*pp) {
    if (strcmp((*pp)->name, name) == 0) {
      cc_node *dead = *pp;
      *pp = dead->next;
      free(dead);
      return;
    }
    pp = &(*pp)->next;
  }
}

void cc_trigger_check(struct discord *client, const struct discord_message *event) {
  if (!event || !event->author || event->author->bot || !event->content)
    return;

  size_t prefix_len = strlen("+");
  if (strncmp(event->content, "+", prefix_len) != 0) return;

  char word[64] = "";
  sscanf(event->content + prefix_len, " %63s", word);
  if (!*word) return;

  cc_node *n = cc_lookup(word);
  if (!n) return;

  if (n->cooldown > 0) {
    time_t now = time(NULL);
    if (now < 0)
      return;

    int64_t remaining = 0;
    customcom_cooldown_result result = customcom_cooldown_try_acquire(
        db, (uint64_t)event->author->id, word, n->cooldown,
        (int64_t)now, &remaining);
    if (result == CUSTOMCOM_COOLDOWN_ERROR) {
      fprintf(stderr, "cc: cooldown state could not be updated; command denied\\n");
      return;
    }
    if (result == CUSTOMCOM_COOLDOWN_ACTIVE) {
      char notice[160];
      snprintf(notice, sizeof notice,
               "Please wait %lld more second(s) before using +%s again.",
               (long long)remaining, word);
      struct discord_create_message limited = {.content = notice};
      discord_create_message(client, event->channel_id, &limited, NULL);
      return;
    }
  }

  struct discord_create_message reply = { .content = n->response };
  discord_create_message(client, event->channel_id, &reply, NULL);
}
