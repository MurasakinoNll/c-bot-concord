#ifndef UTILS_H
#define UTILS_H
#include <concord/discord_codecs.h>
#include <concord/types.h>
#include <stdbool.h>
typedef struct {
  u64snowflake uid;
  u64snowflake *roleptr;
  u64snowflake baseperm;
  int rolecount;
} UserCtx;

bool check_perm_byrole(const UserCtx *ctx, u64snowflake allowlist[], int allowrole_c);

UserCtx get_ctx_from_interaction( const struct discord_interaction *event);
UserCtx get_ctx_from_message(const struct discord_message *event);
bool ctx_has_role(const UserCtx *ctx, u64snowflake roleid);
bool has_admin_bit(const UserCtx *ctx);
#endif
