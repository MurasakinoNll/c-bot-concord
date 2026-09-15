#ifndef SERVERPORT_H
#define SERVERPORT_H

#include <concord/discord.h>

void rolesync_db_init(void);
void rolesync_command(struct discord *client,
                      const struct discord_message *event);
void on_guild_member_add_rolesync(struct discord *client,
                                  const struct discord_guild_member *event);
static void rolesync_generate(struct discord *client,
                              const struct discord_message *event,
                              const char *args);
#endif
