#ifndef CUSTOMCOM_H
#define CUSTOMCOM_H

#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <sqlite3.h>

sqlite3 *customcom_get_db(void);
void customcom_init(void);

void cc_dispatch(struct discord *client, const struct discord_message *event);

void cc_create(struct discord *client, const struct discord_message *event, const char *args);
void cc_delete(struct discord *client, const struct discord_message *event, const char *args);
void cc_search(struct discord *client, const struct discord_message *event, const char *args);
void cc_cooldown(struct discord *client, const struct discord_message *event, const char *args);
void cc_trigger_check(struct discord *client, const struct discord_message *event);
#endif /* CUSTOMCOM_H */
