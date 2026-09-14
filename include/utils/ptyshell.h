#ifndef PTYSHELL_H
#define PTYSHELL_H

#include <concord/discord.h>
#include <sys/types.h>
#include <stdbool.h>

void ptystart_command(struct discord *client, const struct discord_message *event);
void ptystop_command(struct discord *client, const struct discord_message *event);
void ptyinput_fallback(struct discord *client, const struct discord_message *event);

#endif
