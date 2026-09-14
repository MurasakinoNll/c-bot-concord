#ifndef ROLEUTILS_H
#define ROLEUTILS_H
#include <concord/discord.h>
#include <concord/discord_codecs.h>
void role_create(struct discord *client, const struct discord_message *event);
void role_delete(struct discord *client, const struct discord_message *event);

void role_member_add(struct discord *client, const struct discord_message *event);
void role_member_remove(struct discord *client, const struct discord_message *event);
void verify(struct discord *client, const struct discord_message *event);

void undungeon(struct discord*client, const struct discord_message *event);
void dungeon(struct discord *client, const struct discord_message *event);

void cocverify(struct discord *client, const struct discord_message *event);

#endif // !ROLEUTILS_H
