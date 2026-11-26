#include "useful/gpt.h"
#include <useful/cJSON.h>
#include <concord/log.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPT_SETTINGS_PATH "settings.json"
#define GPT_ENDPOINT_FMT                                                       \
  "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"
#define GPT_DEFAULT_MODEL "gemini-2.0-flash"
#define GPT_TIMEOUT_SECS 25L
#define GPT_MAX_PROMPT 1500
#define GPT_DISCORD_MAX 1900

static char *g_api_key = NULL;
static char *g_model = NULL;

struct growbuf {
  char *data;
  size_t size;
};

static size_t gpt_write_cb(void *ptr, size_t size, size_t nmemb,
                           void *userdata) {
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

void gpt_init(void) {
  FILE *fp = fopen(GPT_SETTINGS_PATH, "rb");
  if (!fp) {
    log_warn("gpt: %s not found, +gpt will be disabled", GPT_SETTINGS_PATH);
    return;
  }

  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  rewind(fp);
  if (len <= 0) {
    fclose(fp);
    log_warn("gpt: %s is empty", GPT_SETTINGS_PATH);
    return;
  }

  char *raw = malloc((size_t)len + 1);
  if (!raw) {
    fclose(fp);
    return;
  }
  size_t got = fread(raw, 1, (size_t)len, fp);
  raw[got] = '\0';
  fclose(fp);

  cJSON *root = cJSON_Parse(raw);
  free(raw);
  if (!root) {
    log_error("gpt: %s is not valid json", GPT_SETTINGS_PATH);
    return;
  }

  cJSON *key = cJSON_GetObjectItem(root, "gemini_api_key");
  if (cJSON_IsString(key) && key->valuestring && *key->valuestring) {
    g_api_key = strdup(key->valuestring);
  } else {
    log_error("gpt: no \"gemini_api_key\" in %s", GPT_SETTINGS_PATH);
  }

  cJSON *model = cJSON_GetObjectItem(root, "gemini_model");
  g_model = strdup((cJSON_IsString(model) && model->valuestring &&
                    *model->valuestring)
                       ? model->valuestring
                       : GPT_DEFAULT_MODEL);

  cJSON_Delete(root);

  if (g_api_key) log_info("gpt: ready, model=%s", g_model);
}

static void gpt_send_chunked(struct discord *client, u64snowflake channel_id,
                             const char *text) {
  size_t len = strlen(text);
  size_t pos = 0;

  while (pos < len) {
    size_t remaining = len - pos;
    size_t take = remaining > GPT_DISCORD_MAX ? GPT_DISCORD_MAX : remaining;
    size_t cut = take;

    /* prefer breaking on a newline so code blocks/paragraphs survive */
    if (take == GPT_DISCORD_MAX) {
      for (size_t i = take; i > 0; i--) {
        if (text[pos + i - 1] == '\n') {
          cut = i;
          break;
        }
      }
    }

    char *block = malloc(cut + 1);
    if (!block) return;
    memcpy(block, text + pos, cut);
    block[cut] = '\0';

    struct discord_create_message params = {.content = block};
    discord_create_message(client, channel_id, &params, NULL);

    free(block);
    pos += cut;
  }
}

/* {"contents":[{"parts":[{"text":"<prompt>"}]}]}
 * built with cJSON so the prompt is escaped properly */
static char *gpt_build_body(const char *prompt) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;

  cJSON *contents = cJSON_AddArrayToObject(root, "contents");
  cJSON *turn = cJSON_CreateObject();
  cJSON *parts = cJSON_AddArrayToObject(turn, "parts");
  cJSON *part = cJSON_CreateObject();
  cJSON_AddStringToObject(part, "text", prompt);
  cJSON_AddItemToArray(parts, part);
  cJSON_AddItemToArray(contents, turn);

  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return body;
}

void gpt_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;

  if (!g_api_key) {
    struct discord_create_message reply = {
        .content = "gpt is not configured (missing settings.json)"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  if (!event->content || !*event->content) {
    struct discord_create_message reply = {.content = "usage: +gpt <prompt>"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  if (strlen(event->content) > GPT_MAX_PROMPT) {
    struct discord_create_message reply = {.content = "prompt too long"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  char *body = gpt_build_body(event->content);
  if (!body) return;

  CURL *curl = curl_easy_init();
  if (!curl) {
    free(body);
    return;
  }

  char url[512];
  snprintf(url, sizeof url, GPT_ENDPOINT_FMT, g_model);

  /* key goes in a header, not the query string, so it stays out of logs */
  char keyhdr[256];
  snprintf(keyhdr, sizeof keyhdr, "x-goog-api-key: %s", g_api_key);

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, keyhdr);

  struct growbuf g = {0};
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, gpt_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &g);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "cfbot/1.0");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, GPT_TIMEOUT_SECS);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  free(body);

  if (res != CURLE_OK || !g.data) {
    char buf[128];
    snprintf(buf, sizeof buf, "request failed: %s", curl_easy_strerror(res));
    struct discord_create_message reply = {.content = buf};
    discord_create_message(client, event->channel_id, &reply, NULL);
    free(g.data);
    return;
  }

  cJSON *root = cJSON_Parse(g.data);
  free(g.data);
  if (!root) {
    struct discord_create_message reply = {
        .content = "couldn't parse the api response"};
    discord_create_message(client, event->channel_id, &reply, NULL);
    return;
  }

  /* gemini reports failures as {"error":{"message":...}} */
  cJSON *err = cJSON_GetObjectItem(root, "error");
  if (err) {
    cJSON *emsg = cJSON_GetObjectItem(err, "message");
    char buf[512];
    snprintf(buf, sizeof buf, "api error (%ld): %s", http_code,
             (cJSON_IsString(emsg) && emsg->valuestring) ? emsg->valuestring
                                                         : "unknown");
    struct discord_create_message reply = {.content = buf};
    discord_create_message(client, event->channel_id, &reply, NULL);
    cJSON_Delete(root);
    return;
  }

  /* candidates[0].content.parts[0].text */
  cJSON *cands = cJSON_GetObjectItem(root, "candidates");
  cJSON *cand = cJSON_IsArray(cands) ? cJSON_GetArrayItem(cands, 0) : NULL;

  if (!cand) {
    /* prompt blocked by safety filters -> no candidates at all */
    cJSON *fb = cJSON_GetObjectItem(root, "promptFeedback");
    cJSON *reason = fb ? cJSON_GetObjectItem(fb, "blockReason") : NULL;
    char buf[256];
    snprintf(buf, sizeof buf, "no response (%s)",
             (cJSON_IsString(reason) && reason->valuestring)
                 ? reason->valuestring
                 : "blocked or empty");
    struct discord_create_message reply = {.content = buf};
    discord_create_message(client, event->channel_id, &reply, NULL);
    cJSON_Delete(root);
    return;
  }

  cJSON *content = cJSON_GetObjectItem(cand, "content");
  cJSON *parts = content ? cJSON_GetObjectItem(content, "parts") : NULL;
  cJSON *part = cJSON_IsArray(parts) ? cJSON_GetArrayItem(parts, 0) : NULL;
  cJSON *text = part ? cJSON_GetObjectItem(part, "text") : NULL;

  if (!cJSON_IsString(text) || !text->valuestring || !*text->valuestring) {
    /* a candidate with no text usually means it hit a finishReason */
    cJSON *fr = cJSON_GetObjectItem(cand, "finishReason");
    char buf[256];
    snprintf(buf, sizeof buf, "empty response (%s)",
             (cJSON_IsString(fr) && fr->valuestring) ? fr->valuestring
                                                     : "unknown reason");
    struct discord_create_message reply = {.content = buf};
    discord_create_message(client, event->channel_id, &reply, NULL);
    cJSON_Delete(root);
    return;
  }

  gpt_send_chunked(client, event->channel_id, text->valuestring);
  cJSON_Delete(root);
}
