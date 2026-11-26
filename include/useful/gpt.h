#ifndef GPT_H
#define GPT_H

#include <concord/discord.h>

/* loads settings.json (api key + model). call once from main() before
 * discord_run. safe to call when the file is missing -- +gpt will just
 * report that it isn't configured. */
void gpt_init(void);

void gpt_command(struct discord *client, const struct discord_message *event);

#endif
