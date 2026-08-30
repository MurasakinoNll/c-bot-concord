#include "urban.h"
#include <cJSON.h>
#include <ctype.h>
#include <curl/curl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct growbuf { char *data; size_t size; };

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
  struct growbuf *g = userdata;
  size_t total = size * nmemb;
  char *tmp = realloc(g->data, g->size + total + 1);
  if (!tmp) return 0;
  g->data = tmp;
  memcpy(g->data + g->size, ptr, total);
  g->size += total;
  g->data[g->size] = '\0';
  return total;
}

static void urlencode(const char *in, char *out, size_t out_sz) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 4 < out_sz; i++) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[j++] = c;
    } else {
      j += snprintf(out + j, out_sz - j, "%%%02X", c);
    }
  }
  out[j] = '\0';
}

static cJSON *fetch_definitions(const char *term) {
  char encoded[256];
  urlencode(term, encoded, sizeof encoded);

  char url[512];
  snprintf(url, sizeof url, "https://api.urbandictionary.com/v0/define?term=%s", encoded);

  CURL *curl = curl_easy_init();
  if (!curl) return NULL;

  struct growbuf g = {0};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &g);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "cfbot/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || !g.data) {
    free(g.data);
    return NULL;
  }

  cJSON *root = cJSON_Parse(g.data);
  free(g.data);
  return root;
}

static void build_urban_buttons(struct discord_component buttons[3], char prev_id[100],
                                 char next_id[100], char close_id[100],
                                 u64snowflake requester_id, const char *term, int page) {
  snprintf(prev_id, 100, "urbanpg:p:%d:%" PRIu64 ":%s", page, requester_id, term);
  snprintf(next_id, 100, "urbanpg:n:%d:%" PRIu64 ":%s", page, requester_id, term);
  snprintf(close_id, 100, "urbanpg:x:%d:%" PRIu64 ":%s", page, requester_id, term);

  buttons[0] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_SECONDARY, .label = "◀", .custom_id = prev_id
  };
  buttons[1] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_DANGER, .label = "❌", .custom_id = close_id
  };
  buttons[2] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_SECONDARY, .label = "▶", .custom_id = next_id
  };
}

static void send_urban_page(struct discord *client, u64snowflake channel_id, u64snowflake requester_id,
                             const char *term, int page, bool is_edit,
                             u64snowflake interaction_id, const char *interaction_token) {
  cJSON *root = fetch_definitions(term);
  cJSON *list = root ? cJSON_GetObjectItem(root, "list") : NULL;
  int count = list ? cJSON_GetArraySize(list) : 0;

  if (count == 0) {
    struct discord_create_message reply = { .content = "No definitions found." };
    if (is_edit) {
      struct discord_interaction_response resp = {
        .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
        .data = &(struct discord_interaction_callback_data){ .content = "No definitions found.", .embeds = NULL, .components = NULL },
      };
      discord_create_interaction_response(client, interaction_id, interaction_token, &resp, NULL);
    } else {
      discord_create_message(client, channel_id, &reply, NULL);
    }
    if (root) cJSON_Delete(root);
    return;
  }

  if (page < 0) page = 0;
  if (page >= count) page = count - 1;

  cJSON *entry = cJSON_GetArrayItem(list, page);
  const char *word       = cJSON_GetObjectItem(entry, "word")->valuestring;
  const char *definition = cJSON_GetObjectItem(entry, "definition")->valuestring;
  const char *example    = cJSON_GetObjectItem(entry, "example")->valuestring;
  const char *author     = cJSON_GetObjectItem(entry, "author")->valuestring;
  int thumbs_up   = cJSON_GetObjectItem(entry, "thumbs_up")->valueint;
  int thumbs_down = cJSON_GetObjectItem(entry, "thumbs_down")->valueint;

  char desc[3000], footer[64];
  snprintf(desc, sizeof desc, "%.1800s\n\n**Example:**\n%.900s", definition, example);
  snprintf(footer, sizeof footer, "%d/%d \xE2\x80\xA2 \xF0\x9F\x91\x8D%d \xF0\x9F\x91\x8E%d \xE2\x80\xA2 by %.20s",
           page + 1, count, thumbs_up, thumbs_down, author);

  struct discord_embed embed = {
    .title = (char*)word, .description = desc, .color = 0x1D2439,
    .footer = &(struct discord_embed_footer){ .text = footer },
  };

  char prev_id[100], next_id[100], close_id[100];
  struct discord_component buttons[3];
  build_urban_buttons(buttons, prev_id, next_id, close_id, requester_id, term, page);
  struct discord_components button_list = { .size = 3, .array = buttons };
  struct discord_component action_row = { .type = DISCORD_COMPONENT_ACTION_ROW, .components = &button_list };
  struct discord_components row_list = { .size = 1, .array = &action_row };

  if (is_edit) {
    struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
      .data = &(struct discord_interaction_callback_data){
        .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
        .components = &row_list,
      },
    };
    discord_create_interaction_response(client, interaction_id, interaction_token, &resp, NULL);
  } else {
    struct discord_create_message params = {
      .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
      .components = &row_list,
    };
    discord_create_message(client, channel_id, &params, NULL);
  }

  cJSON_Delete(root);
}

void urban_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;

  char term[128] = "";
  sscanf(event->content, " %127[^\n]", term);
  if (!*term) {
    struct discord_create_message reply = { .content = "Usage: +urban <term>" };
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  send_urban_page(client, event->channel_id, event->author->id, term, 0, false, 0, NULL);
}

void urban_handle_interaction(struct discord *client, const struct discord_interaction *event) {
  if (!event->data || !event->data->custom_id) return;
  if (strncmp(event->data->custom_id, "urbanpg:", 8) != 0) return;

  char action[4] = "", term[128] = "";
  int page = 0;
  u64snowflake requester_id = 0;
  sscanf(event->data->custom_id, "urbanpg:%3[^:]:%d:%" SCNu64 ":%127[^\n]",
         action, &page, &requester_id, term);

  u64snowflake clicker_id = (event->member && event->member->user) ? event->member->user->id : 0;
  if (clicker_id != requester_id) {
    struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
      .data = &(struct discord_interaction_callback_data){
        .content = "Only the person who ran this command can use these buttons.",
        .flags = DISCORD_MESSAGE_EPHEMERAL,
      },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
    return;
  }

  if (strcmp(action, "x") == 0) {
    struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
      .data = &(struct discord_interaction_callback_data){ .content = "Closed.", .embeds = NULL, .components = NULL },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
    return;
  }

  if (strcmp(action, "n") == 0) page++;
  else if (strcmp(action, "p") == 0) page--;

  send_urban_page(client, event->channel_id, requester_id, term, page, true, event->id, event->token);
}
