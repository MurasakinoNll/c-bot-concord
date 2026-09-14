#ifndef CHANNELUTILS_H
#define CHANNELUTILS_H

#include <concord/discord.h>
#include <concord/discord_codecs.h>
void ticket_db_init(void);
void channel_create(struct discord *client, const struct discord_interaction *event);
void close_ticket(struct discord *client, const struct discord_message *event);
#endif
