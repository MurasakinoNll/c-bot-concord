#include <concord/discord.h>
#include <time.h>
#include <inttypes.h>

void ping_command(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  struct discord_message sent_msg = {0};
  struct discord_ret_message ret = { .sync = &sent_msg };

  struct discord_create_message params = { .content = "Pong. returning REST+gateway_ms" };
  discord_create_message(client, event->channel_id, &params, &ret);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  long rest_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;

  int gateway_ms = discord_get_ping(client);

  char rest_buf[32], gw_buf[32];
  snprintf(rest_buf, sizeof rest_buf, "%ldms", rest_ms);
  snprintf(gw_buf, sizeof gw_buf, "%dms", gateway_ms);

  struct discord_embed embed = { .title = "Pong.", .color = 0x57F287 };
  discord_embed_add_field(&embed, "REST latency", rest_buf, true);
  discord_embed_add_field(&embed, "Gateway latency", gw_buf, true);

  struct discord_edit_message edit_params = {
    .content = "",
    .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
  };
  discord_edit_message(client, event->channel_id, sent_msg.id, &edit_params, NULL);
}
