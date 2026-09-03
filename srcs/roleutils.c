#include "utils.h"
#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/guild.h>
#include <concord/log.h>
#include <concord/types.h>
#include <stdio.h>
#include <string.h>

static void on_role_add_done(struct discord *client, struct discord_response *resp) {
  u64snowflake channel_id = (u64snowflake)(intptr_t)resp->data;
  struct discord_create_message params = { .content = "done." };
  discord_create_message(client, channel_id, &params, NULL);
}
static void on_role_add_hamudi(struct discord *client, struct discord_response *resp) {
  u64snowflake channel_id = (u64snowflake)(intptr_t)resp->data;
  struct discord_create_message params = { .content = "fr" };
  discord_create_message(client, channel_id, &params, NULL);
}
static void on_role_add_fail(struct discord *client, struct discord_response *resp) {
  u64snowflake channel_id = (u64snowflake)(intptr_t)resp->data;
  char buf[256];
  snprintf(buf, sizeof buf, "err: %s", discord_strerror(resp->code, client));
  struct discord_create_message params = { .content = buf };
  discord_create_message(client, channel_id, &params, NULL);
}

void role_create(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {
  1155152569526669391ULL
  };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "role_create rejected, invalid permissions\n");
    return;
  }

  char name[128] = "";
  sscanf(event->content," %127[^\n]", name);
  if (!*name){
  log_error("Couldnt create role %s", name);
    return;
  }

  struct discord_create_guild_role params ={
    .name = name,
  };
  discord_create_guild_role(client, event->guild_id, &params, NULL);
}

void role_delete(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {
  1155152569526669391ULL
  };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "role_delete rejected, invalid permissions\n");
    return;
  }

  if (!event->mention_roles || event->mention_roles->size < 1) {
    fprintf(stderr, "role_delete: no role mentioned\n");
    return;
  }
  
  u64snowflake roleid = event->mention_roles->array[0];
  struct discord_ret ret = {
    .done = &on_role_add_done,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  struct discord_delete_guild_role params = { .reason = "stinky role" };
  discord_delete_guild_role(client, event->guild_id, roleid, &params, &ret);
}

void role_member_add(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {
  1155152569526669391ULL
  };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "role_member_add rejected, invalid permissions\n");
    return;
  }

  if (!event->mentions || event->mentions->size < 1) {
    fprintf(stderr, "role_member_add: no user mentioned\n");
    return;
  }
  if (!event->mention_roles || event->mention_roles->size < 1) {
    fprintf(stderr, "role_member_add: no role mentioned\n");
    return;
  }

  u64snowflake uid    = event->mentions->array[0].id;
  u64snowflake roleid = event->mention_roles->array[0];  struct discord_add_guild_member_role params = {.reason = "role_member_add"};
  struct discord_ret ret = {
    .done = &on_role_add_done,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  discord_add_guild_member_role(client, event->guild_id, uid, roleid, &params, &ret);
}

void role_member_remove(struct discord *client, const struct discord_message *event){
  if(event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = {
  1155152569526669391ULL
  };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "role_member_remove rejected, invalid permissions\n");
    return;
  }

  
  if (!event->mentions || event->mentions->size < 1) {
    fprintf(stderr, "role_member_remove: no user mentioned\n");
    return;
  }
  if (!event->mention_roles || event->mention_roles->size < 1) {
    fprintf(stderr, "role_member_remove: no role mentioned\n");
    return;
  }

  u64snowflake uid    = event->mentions->array[0].id;
  u64snowflake roleid = event->mention_roles->array[0];

  struct discord_ret ret = {
    .done = &on_role_add_done,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  struct discord_remove_guild_member_role params = {.reason = "role_member_remove"};
  discord_remove_guild_member_role(client, event->guild_id, uid, roleid, &params, &ret);
}
void dungeon(struct discord *client, const struct discord_message *event){
  #define DUNGEONR 1155152569560211480ULL
  #define VERIFIED 1155156368957902958ULL
  if (event->author->bot) return;
  struct discord_ret ret = {
    .done = &on_role_add_hamudi,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  discord_add_guild_member_role(client, event->guild_id, 1255503080842330264, DUNGEONR,&(struct discord_add_guild_member_role){0}, &ret);
  discord_remove_guild_member_role(client, event->guild_id, 1255503080842330264, VERIFIED, &(struct discord_remove_guild_member_role){0}, &ret);
}
void undungeon(struct discord*client, const struct discord_message *event){
   #define DUNGEONR 1155152569560211480ULL
  #define VERIFIED 1155156368957902958ULL
  if (event->author->bot) return;
  if (event->author->id == 1255503080842330264) return;
  struct discord_ret ret = {
    .done = &on_role_add_hamudi,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  discord_add_guild_member_role(client, event->guild_id, 1255503080842330264, VERIFIED,&(struct discord_add_guild_member_role){0}, &ret);
  discord_remove_guild_member_role(client, event->guild_id, 1255503080842330264, DUNGEONR, &(struct discord_remove_guild_member_role){0}, &ret);
}

void cocverify(struct discord *client, const struct discord_message *event){
if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL, 1235277600151179364ULL };
  if (!check_perm_byrole(&ctx, allowlist, 2)){
    fprintf(stderr, "cocverify rejected, invalid permissions\n");
    return;
  }
  u64snowflake uid = 0;
  if (event->mentions && event->mentions->size > 0) {
    uid = event->mentions->array[0].id;
  } else {
    sscanf(event->content, " %" SCNu64, &uid);
  }  if (!uid){
    fprintf(stderr, "verify invalid uid\n");
    return;
  }

  struct discord_ret ret = {
    .done = &on_role_add_done,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };

  #define COCCAT 1376000578269741156ULL
  #define COCMEMBER 1234185321671954585ULL
  #define UNVERIFIED 1206585974956490802ULL
  
  discord_add_guild_member_role(client, event->guild_id, uid, COCCAT,&(struct discord_add_guild_member_role){0}, NULL);
  discord_add_guild_member_role(client, event->guild_id, uid, COCMEMBER,&(struct discord_add_guild_member_role){0}, NULL);
  discord_remove_guild_member_role(client, event->guild_id, uid, UNVERIFIED, &(struct discord_remove_guild_member_role){0}, &ret);

}

void verify(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  UserCtx ctx = get_ctx_from_message(event);
  u64snowflake allowlist[] = { 1155152569526669391ULL };
  if (!check_perm_byrole(&ctx, allowlist, 1)){
    fprintf(stderr, "verify rejected, invalid permissions\n");
    return;
  }
  u64snowflake uid = 0;
  if (event->mentions && event->mentions->size > 0) {
    uid = event->mentions->array[0].id;
  } else {
    sscanf(event->content, " %" SCNu64, &uid);
  }  if (!uid){
    fprintf(stderr, "verify invalid uid\n");
    return;
  }
  #define VERIFIED 1155156368957902958ULL
  #define UNVERIFIED 1206585974956490802ULL
  #define CAT1 1516389058710994944ULL
  #define CAT2 1273938959524958328ULL
  #define CAT3 1273941308683190324ULL
  #define CAT4 1155152569451159622ULL
  #define GUEST 1155152569526669385ULL


  struct discord_ret ret = {
    .done = &on_role_add_done,
    .fail = &on_role_add_fail,
    .data = (void*)(intptr_t)event->channel_id,
  };
  discord_add_guild_member_role(client, event->guild_id, uid, GUEST,&(struct discord_add_guild_member_role){0}, NULL);
  discord_add_guild_member_role(client, event->guild_id, uid, CAT1,&(struct discord_add_guild_member_role){0}, NULL);
  discord_add_guild_member_role(client, event->guild_id, uid, CAT2 ,&(struct discord_add_guild_member_role){0}, NULL);
  discord_add_guild_member_role(client, event->guild_id, uid, CAT3,&(struct discord_add_guild_member_role){0}, NULL);
  discord_add_guild_member_role(client, event->guild_id, uid, CAT4,&(struct discord_add_guild_member_role){0}, NULL);
  discord_add_guild_member_role(client, event->guild_id, uid, VERIFIED,&(struct discord_add_guild_member_role){0}, NULL);
  discord_remove_guild_member_role(client, event->guild_id, uid, UNVERIFIED, &(struct discord_remove_guild_member_role){0}, &ret);
}
