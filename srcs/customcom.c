#include <concord/discord.h>
#include <sqlite3.h>
#include <string.h>
#include "customcom.h"
#include "utils.h"
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
  sqlite3_open("customcoms.db", &db);
  sqlite3_exec(db,
               "CREATE TABLE IF NOT EXISTS customcoms ("
               "name TEXT PRIMARY KEY, response TEXT NOT NULL, "
               "cooldown INTEGER DEFAULT 0, created_by TEXT, uses INTEGER DEFAULT 0);",
               NULL, NULL, NULL);
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT name, response, cooldown FROM customcoms", -1, &stmt, NULL);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    cc_cache_insert((const char*)sqlite3_column_text(stmt, 0),
                    (const char*)sqlite3_column_text(stmt, 1),
                  sqlite3_column_int(stmt, 2));
}
sqlite3_finalize(stmt);
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

void cc_search(struct discord *client, const struct discord_message *event, const char *args) {
  char query[64] = "";
  sscanf(args, " %63s", query);   /* empty if no args — list-all case */

  char pattern[68];
  snprintf(pattern, sizeof pattern, "%%%s%%", query);   /* SQL LIKE wildcard wrap */

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT name FROM customcoms WHERE name LIKE ? ORDER BY name", -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);

  char out[1900] = "";   /* stay under Discord's 2000-char message limit */
  strcat(out, "Custom commands:\n");
  int found = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *name = (const char*)sqlite3_column_text(stmt, 0);
    strncat(out, name, sizeof(out) - strlen(out) - 2);
    strcat(out, "\n");
    found = 1;
  }
  sqlite3_finalize(stmt);

  struct discord_create_message reply = { .content = found ? out : "cc not found" };
  discord_create_message(client, event->channel_id, &reply, NULL);
}
void cc_cooldown(struct discord *client, const struct discord_message *event, const char *args) {
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "cc_cooldown rejected, invalid permissions\n");
    return;
  }

  char name[64] = "";
  int seconds = -1;
  int matched = sscanf(args, " %63s %d", name, &seconds);

  if (matched < 1 || !*name) {
    fprintf(stderr, "cc cooldown: usage <name> [seconds]\n");
    return;
  }

  char buf[128];
  sqlite3_stmt *stmt;

  if (matched == 1) {
    /* view mode */
    sqlite3_prepare_v2(db, "SELECT cooldown FROM customcoms WHERE name = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      snprintf(buf, sizeof buf, "Cooldown for '%s': %ds", name, sqlite3_column_int(stmt, 0));
    } else {
      snprintf(buf, sizeof buf, "No command named '%s'.", name);
    }
    sqlite3_finalize(stmt);
  } else {
    /* set mode */
    sqlite3_prepare_v2(db, "UPDATE customcoms SET cooldown = ? WHERE name = ?", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, seconds);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (changed > 0) {
      snprintf(buf, sizeof buf, "Cooldown for '%s' set to %ds.", name, seconds);
      cc_cache_insert(name, cc_lookup(name)->response, seconds);   // buggy section relating to under
    } else {
      snprintf(buf, sizeof buf, "No command named '%s'.", name);
    }
  }

  struct discord_create_message reply = { .content = buf };
  discord_create_message(client, event->channel_id, &reply, NULL);
}/* 
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
  cc_node *existing = cc_lookup(name);
  if (existing) {
    strncpy(existing->response, response, sizeof(existing->response) - 1);
    existing->cooldown = cooldown;
    return;
  }
  cc_node *n = calloc(1, sizeof *n);
  strncpy(n->name, name, sizeof(n->name) - 1);
  strncpy(n->response, response, sizeof(n->response) - 1);
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
  if (event->author->bot) return;

  size_t prefix_len = strlen("+");
  if (strncmp(event->content, "+", prefix_len) != 0) return;

  char word[64] = "";
  sscanf(event->content + prefix_len, " %63s", word);
  if (!*word) return;

  cc_node *n = cc_lookup(word);
  if (!n) return;

  struct discord_create_message reply = { .content = n->response };
  discord_create_message(client, event->channel_id, &reply, NULL);
}
