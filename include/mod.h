#ifndef MOD_H
#define MOD_H

#include <concord/discord.h>
#include <concord/discord_codecs.h>
void ban(struct discord *client, const struct discord_message *event);
void mute(struct discord *client, const struct discord_message *event);
void unban(struct discord *client, const struct discord_message *event);
void unmute(struct discord *client, const struct discord_message *event);
#endif // !MOD_H
