#ifndef MSGLIMIT_H
#define MSGLIMIT_H

#include <concord/discord.h>
#include <stdbool.h>

void msglimit_db_init(void);
void msglimit_command(struct discord *client, const struct discord_message *event);
bool msglimit_enforce(struct discord *client, const struct discord_message *event);

#endif
