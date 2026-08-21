#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/types.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "utils.h"
static void on_modaction_success(struct discord *client, struct discord_response *resp) {
  u64snowflake channel_id = (u64snowflake)(intptr_t)resp->data;
  char buf[256];
snprintf(buf, sizeof buf, "err: %s", discord_strerror(resp->code, client));
  struct discord_create_message params = { .content = "modaction success" };
  discord_create_message(client, channel_id, &params, NULL);
}
static void on_modaction_member_success(struct discord *client, struct discord_response *resp, const struct discord_guild_member *mem){
  u64snowflake channel_id = (u64snowflake)(intptr_t)resp->data;
  char buf[256];
  snprintf(buf, sizeof(buf), "modaction succeeded for <@%" PRIu64 ">.", mem->user->id);
  struct discord_create_message params = { .content = buf };
  discord_create_message(client, channel_id, &params, NULL);
}
static void on_modaction_fail(struct discord *client, struct discord_response *resp) {
  u64snowflake channel_id = (u64snowflake)(intptr_t)resp->data;
  char buf[256];
  snprintf(buf, sizeof buf, "err: %s", discord_strerror(resp->code, client));
  struct discord_create_message params = { .content = buf };
  discord_create_message(client, channel_id, &params, NULL);
}

void ban(struct discord *client, const struct discord_message *event){
  
  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {
  1155152569526669391ULL
  };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "ban rejected, invalid permissions\n");
    return;
  }
  u64snowflake targetuid = 0ULL;

  if(event->mentions && event->mentions->size > 0){
    targetuid = event->mentions->array[0].id;
  } else {
    sscanf(event->content, " %" SCNu64, &targetuid);
  }

  struct discord_create_guild_ban params={
    .delete_message_days = 0,
    .reason = "bad kitten",
  };
  struct discord_ret ret = {
  .done = &on_modaction_success,
  .fail = &on_modaction_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  discord_create_guild_ban(client, event->guild_id, targetuid, &params, &ret);
}
void mute(struct discord *client, const struct discord_message *event){
  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {
  1155152569526669391ULL
  };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "mute rejected, invalid permissions\n");
    return;
  }
  u64snowflake uid = 0;
  int minutes = 0;

  if (event->mentions && event->mentions->size > 0) {
    uid = event->mentions->array[0].id;
    sscanf(event->content, "%*s %d", &minutes);
  } else {
    sscanf(event->content, " %" SCNu64 " %d", &uid, &minutes);
  }

  if (!uid || minutes <= 0) {
    fprintf(stderr, "mute: <uid|mention> <minutes>\n");
    return;
  }

  u64unix_ms until_ms = ((u64unix_ms)time(NULL) + (u64unix_ms)(minutes * 60)) * 1000;

  struct discord_modify_guild_member params = {
    .communication_disabled_until = until_ms,
  };
  struct discord_ret_guild_member ret = {
    .done = &on_modaction_member_success,
    .fail = &on_modaction_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  discord_modify_guild_member(client, event->guild_id, uid, &params, &ret);
}

void unmute(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "unmute_command rejected, invalid permissions\n");
    return;
  }

  u64snowflake uid = 0;
  if (event->mentions && event->mentions->size > 0) {
    uid = event->mentions->array[0].id;
  } else {
    sscanf(event->content, " %" SCNu64, &uid);
  }
  if (!uid) {
    fprintf(stderr, "unmute <uid|mention>\n");
    return;
  }

  struct discord_modify_guild_member params = {
    .communication_disabled_until = 0,
  };
  struct discord_ret_guild_member ret = {
  .done = &on_modaction_member_success,
  .fail = &on_modaction_fail,
  .data = (void*)(intptr_t)event->channel_id,
  };
  discord_modify_guild_member(client, event->guild_id, uid, &params, &ret);
}

void unban(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "unmute_command rejected, invalid permissions\n");
    return;
  }

  u64snowflake uid = 0ULL;
    if (event->mentions && event->mentions->size > 0) {
    uid = event->mentions->array[0].id;
  } else {
    sscanf(event->content, " %" SCNu64, &uid);
  }
  if (!uid) {
    fprintf(stderr, "unban <uid|mention>\n");
    return;
  }
  if(!uid){
  fprintf(stderr, "unban <id|mention>");
    return;
  }
  struct discord_ret ret = {
  .done = &on_modaction_success,
    .fail = &on_modaction_fail,
  .data = (void*)(intptr_t)event->channel_id,
  };
  struct discord_remove_guild_ban params = {
    .reason = "not stinky",
  };
  discord_remove_guild_ban(client, event->guild_id, uid, &params, &ret); 
}
