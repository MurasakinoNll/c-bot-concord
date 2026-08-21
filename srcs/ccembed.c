#include <concord/discord_codecs.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include "customcom.h"
#include "ccembed.h"

#define CC_PAGE_SIZE 13

static int cc_fetch_page(const char *query, int *page, char *desc, size_t desc_sz,
                          char *footer, size_t footer_sz) {
  sqlite3 *db = customcom_get_db();
  char pattern[68];
  snprintf(pattern, sizeof pattern, "%%%s%%", query);

  sqlite3_stmt *stmt;
  int total = 0;
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM customcoms WHERE name LIKE ?", -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
  if (sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  int total_pages = (total + CC_PAGE_SIZE - 1) / CC_PAGE_SIZE;
  if (total_pages < 1) total_pages = 1;
  if (*page < 0) *page = 0;
  if (*page >= total_pages) *page = total_pages - 1;

  sqlite3_prepare_v2(db,
    "SELECT name FROM customcoms WHERE name LIKE ? ORDER BY name LIMIT ? OFFSET ?",
    -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, CC_PAGE_SIZE);
  sqlite3_bind_int(stmt, 3, (*page) * CC_PAGE_SIZE);

  desc[0] = '\0';
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    strncat(desc, (const char*)sqlite3_column_text(stmt, 0), desc_sz - strlen(desc) - 2);
    strncat(desc, "\n", desc_sz - strlen(desc) - 1);
  }
  sqlite3_finalize(stmt);
  if (!*desc) strcpy(desc, "cc not found");

  snprintf(footer, footer_sz, "Page %d / %d", *page + 1, total_pages);
  return total_pages;
}

static void build_buttons(struct discord_component buttons[2], char prev_id[100], char next_id[100],
                           u64snowflake requester_id, const char *query, int page) {
  snprintf(prev_id, 100, "ccpg:p:%d:%" PRIu64 ":%s", page, requester_id, query);
  snprintf(next_id, 100, "ccpg:n:%d:%" PRIu64 ":%s", page, requester_id, query);
  buttons[0] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_SECONDARY, .label = "◀", .custom_id = prev_id
  };
  buttons[1] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_SECONDARY, .label = "▶", .custom_id = next_id
  };
}

void ccembed_send_page(struct discord *client, u64snowflake channel_id,
                        u64snowflake requester_id, const char *query, int page) {
  char desc[900], footer[32], prev_id[100], next_id[100];
  cc_fetch_page(query, &page, desc, sizeof desc, footer, sizeof footer);

  struct discord_embed embed = {
    .title = "Custom Commands", .description = desc, .color = 3447003,
    .footer = &(struct discord_embed_footer){ .text = footer },
  };

  struct discord_component buttons[2];
  build_buttons(buttons, prev_id, next_id, requester_id, query, page);
  struct discord_components button_list = { .size = 2, .array = buttons };
  struct discord_component action_row = {
    .type = DISCORD_COMPONENT_ACTION_ROW,
    .components = &button_list,
  };
  struct discord_components row_list = { .size = 1, .array = &action_row };

  struct discord_create_message params = {
    .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
    .components = &row_list,
  };
  discord_create_message(client, channel_id, &params, NULL);
}

void ccembed_handle_interaction(struct discord *client, const struct discord_interaction *event) {
  if (!event->data || !event->data->custom_id) return;
  if (strncmp(event->data->custom_id, "ccpg:", 5) != 0) return;

  char action[4] = "", query[64] = "";
  int page = 0;
  u64snowflake requester_id = 0;
  sscanf(event->data->custom_id, "ccpg:%3[^:]:%d:%" SCNu64 ":%63s",
         action, &page, &requester_id, query);

  u64snowflake clicker_id = (event->member && event->member->user) ? event->member->user->id : 0;

  if (clicker_id != requester_id) {
    struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
      .data = &(struct discord_interaction_callback_data){
        .content = "invalid interactor uid",
        .flags = DISCORD_MESSAGE_EPHEMERAL,
      },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
    return;
  }

  if (strcmp(action, "n") == 0) page++;
  else if (strcmp(action, "p") == 0) page--;

  char desc[900], footer[32], prev_id[100], next_id[100];
  cc_fetch_page(query, &page, desc, sizeof desc, footer, sizeof footer);

  struct discord_embed embed = {
    .title = "__Custom Commands:__", .description = desc, .color = 3447003,
    .footer = &(struct discord_embed_footer){ .text = footer },
  };

  struct discord_component buttons[2];
  build_buttons(buttons, prev_id, next_id, requester_id, query, page);
  struct discord_components button_list = { .size = 2, .array = buttons };
  struct discord_component action_row = {
    .type = DISCORD_COMPONENT_ACTION_ROW,
    .components = &button_list,
  };
  struct discord_components row_list = { .size = 1, .array = &action_row };

  struct discord_interaction_response resp = {
    .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
    .data = &(struct discord_interaction_callback_data){
      .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
      .components = &row_list,
    },
  };
  discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
}
