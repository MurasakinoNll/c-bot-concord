#include "useful/urban.h"
#include <useful/cJSON.h>
#include <ctype.h>
#include <curl/curl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct growbuf { char *data; size_t size; };

#define URBAN_RESPONSE_MAX (64U * 1024U)

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
  struct growbuf *g = userdata;
  if (size && nmemb > SIZE_MAX / size)
    return 0;
  size_t total = size * nmemb;
  if (total > URBAN_RESPONSE_MAX - g->size)
    return 0;
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
  char encoded[512];
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
  /* Keep button interactions within Discord's 3-second initial-response
   * deadline until the exact Concord dependency supports a verified defer/edit
   * flow. This is a bounded mitigation, not a substitute for deferral. */
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 700L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1800L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(curl);
  long http_status = 0;
  if (res == CURLE_OK)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || http_status < 200 || http_status >= 300 || !g.data) {
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
  cJSON *list = root ? cJSON_GetObjectItemCaseSensitive(root, "list") : NULL;
  int count = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : 0;

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
  cJSON *word_item = cJSON_GetObjectItemCaseSensitive(entry, "word");
  cJSON *definition_item =
      cJSON_GetObjectItemCaseSensitive(entry, "definition");
  cJSON *example_item = cJSON_GetObjectItemCaseSensitive(entry, "example");
  cJSON *author_item = cJSON_GetObjectItemCaseSensitive(entry, "author");
  cJSON *up_item = cJSON_GetObjectItemCaseSensitive(entry, "thumbs_up");
  cJSON *down_item = cJSON_GetObjectItemCaseSensitive(entry, "thumbs_down");

  if (!cJSON_IsObject(entry) || !cJSON_IsString(word_item) ||
      !cJSON_IsString(definition_item) || !cJSON_IsString(example_item) ||
      !cJSON_IsString(author_item) || !word_item->valuestring ||
      !definition_item->valuestring || !example_item->valuestring ||
      !author_item->valuestring) {
    struct discord_create_message reply = {
        .content = "The upstream service returned an invalid definition."};
    if (is_edit) {
      struct discord_interaction_response resp = {
          .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
          .data = &(struct discord_interaction_callback_data){
              .content = "The upstream service returned an invalid definition.",
              .embeds = NULL,
              .components = NULL}};
      discord_create_interaction_response(client, interaction_id,
                                          interaction_token, &resp, NULL);
    } else {
      discord_create_message(client, channel_id, &reply, NULL);
    }
    cJSON_Delete(root);
    return;
  }

  const char *word = word_item->valuestring;
  const char *definition = definition_item->valuestring;
  const char *example = example_item->valuestring;
  const char *author = author_item->valuestring;
  int thumbs_up = cJSON_IsNumber(up_item) ? up_item->valueint : 0;
  int thumbs_down = cJSON_IsNumber(down_item) ? down_item->valueint : 0;

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

static bool parse_custom_id_number(const char **cursor, uint64_t *value) {
  if (!cursor || !*cursor || !value || **cursor < '0' || **cursor > '9')
    return false;
  uint64_t parsed = 0;
  const char *p = *cursor;
  do {
    unsigned digit = (unsigned)(*p - '0');
    if (parsed > (UINT64_MAX - digit) / 10U)
      return false;
    parsed = parsed * 10U + digit;
    ++p;
  } while (*p >= '0' && *p <= '9');
  *cursor = p;
  *value = parsed;
  return true;
}

void urban_command(struct discord *client, const struct discord_message *event) {
  if (!event || !event->author || event->author->bot || !event->content)
    return;
  if (event->author->id == 1255503080842330264)
    return;

  char term[128] = "";
  if (sscanf(event->content, " %127[^\n]", term) != 1 || !*term) {
    struct discord_create_message reply = {.content = "Usage: +urban <term>"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }
  if (strlen(term) > 55) {
    struct discord_create_message reply = {
        .content = "Search terms must be 55 bytes or fewer so pagination buttons remain valid."};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  send_urban_page(client, event->channel_id, event->author->id, term, 0, false,
                  0, NULL);
}

void urban_handle_interaction(struct discord *client,
                              const struct discord_interaction *event) {
  if (!event || !event->data || !event->data->custom_id || !event->token ||
      !event->id)
    return;
  const char *custom_id = event->data->custom_id;
  if (strncmp(custom_id, "urbanpg:", 8) != 0)
    return;

  const char *p = custom_id + 8;
  char action = *p++;
  uint64_t page_value = 0, requester_id = 0;
  if ((action != 'p' && action != 'n' && action != 'x') || *p++ != ':' ||
      !parse_custom_id_number(&p, &page_value) || *p++ != ':' ||
      !parse_custom_id_number(&p, &requester_id) || *p++ != ':' ||
      page_value >= (uint64_t)INT_MAX || requester_id == 0 || !*p ||
      strlen(p) > 55)
    return;

  char term[56];
  snprintf(term, sizeof term, "%s", p);
  u64snowflake clicker_id =
      event->member && event->member->user
          ? event->member->user->id
          : event->user ? event->user->id : 0;
  if (clicker_id != requester_id) {
    struct discord_interaction_response resp = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &(struct discord_interaction_callback_data){
            .content = "Only the person who ran this command can use these buttons.",
            .flags = DISCORD_MESSAGE_EPHEMERAL,
        },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp,
                                        NULL);
    return;
  }

  int page = (int)page_value;
  if (action == 'x') {
    struct discord_interaction_response resp = {
        .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
        .data = &(struct discord_interaction_callback_data){
            .content = "Closed.", .embeds = NULL, .components = NULL},
    };
    discord_create_interaction_response(client, event->id, event->token, &resp,
                                        NULL);
    return;
  }

  if (action == 'n')
    ++page;
  else if (page > 0)
    --page;

  send_urban_page(client, event->channel_id, requester_id, term, page, true,
                  event->id, event->token);
}
