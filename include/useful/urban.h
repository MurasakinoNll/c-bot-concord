#ifndef URBAN_H
#define URBAN_H

#include <concord/discord.h>

void urban_command(struct discord *client, const struct discord_message *event);
void urban_handle_interaction(struct discord *client, const struct discord_interaction *event);

#endif
