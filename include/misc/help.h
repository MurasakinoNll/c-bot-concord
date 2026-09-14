#ifndef HELP_H
#define HELP_H

#include <concord/discord.h>

void helper(struct discord *client, const struct discord_message *event);
void help_handle_interaction(struct discord *client, const struct discord_interaction *event);

#endif
