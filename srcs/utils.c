#include "utils.h"
#include <concord/discord_codecs.h>
#include <concord/types.h>
#include <stdbool.h>
#include <stdio.h>
#define ROLE_ADMIN 1155152569526669391
#define ROLE_PRIVILAGED 1235277600151179364

UserCtx get_ctx_from_interaction( const struct discord_interaction *event){
  UserCtx ctx = {0};
  if (event && event->member){
    ctx.uid = event->member->user ? event->member->user->id : 0;
    ctx.baseperm = event->member->permissions;
    if(event->member->roles){
      ctx.roleptr = event->member->roles->array;
      ctx.rolecount = event->member->roles->size;
    }
  }
  return ctx;
}

UserCtx get_ctx_from_message(const struct discord_message *event){
  UserCtx ctx = {0};
  if (event && event->member){
    ctx.uid = event->author ? event->author->id : 0;
    ctx.baseperm = event->member->permissions;
    if (event->member->roles) {
      ctx.roleptr = event->member->roles->array;
      ctx.rolecount = event->member->roles->size;
    }
  }
  return ctx;
}

bool check_perm_byrole(const UserCtx *ctx, u64snowflake allowlist[], int allowrole_c){
  if (has_admin_bit(ctx)) return  true;
  for (int i = 0; i < allowrole_c; i++){
    if (ctx_has_role(ctx, allowlist[i])) return true;
  }
  return false;
}

bool ctx_has_role(const UserCtx *ctx, u64snowflake roleid){
  if (!ctx || !ctx->roleptr) return false;
  for (int i=0; i < ctx->rolecount; i++){
    if(ctx->roleptr[i] == roleid) return true;
  }
  return false;
}

bool has_admin_bit(const UserCtx *ctx){
  if (!ctx) return false;
  fprintf(stderr, "ctx: %lu\n", ctx->baseperm);
  return (ctx->baseperm & DISCORD_PERM_ADMINISTRATOR)!=0;
}
