#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/types.h>
#ifndef TICKETSYSTEM_H
#define TICKETSYSTEM_H
int load_ticketcounter(void);
void ticketinit(struct discord *client, const struct discord_message *event);
void welcome_message(struct discord *client, u64snowflake targetchannel);
void json_builder(struct discord *client, const struct discord_message *event);
void on_interaction_create(struct discord *client, const struct discord_interaction *event);
#endif
