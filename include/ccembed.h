#ifndef CCEMBED_H
#define CCEMBED_H

#include <concord/discord.h>


void ccembed_send_page(struct discord *client, u64snowflake channel_id,
                        u64snowflake requester_id, const char *query, int page);
void ccembed_handle_interaction(struct discord *client, const struct discord_interaction *event);

#endif
