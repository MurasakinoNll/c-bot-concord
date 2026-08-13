#include <assert.h>
// #include <concord/concord-once.h>
#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <time.h>
u64snowflake g_app_id;

void on_ready(struct discord *client, const struct discord_ready *event) {
  log_info("main bot connected to discord as %s#%s", event->user->username,
           event->user->discriminator);
  g_app_id = event->application->id;
}
char JSON[] = "{\n"
              "  \"title\": \"garak title\",\n"
              "  \"description\": \"testing desc\", \n"
              "  \"url\": \"http://127.0.0.1:8080/\", \n"
              "  \"color\" 3446043, \n"
              "}";

void on_dynamic(struct discord *client, const struct discord_message *event) {
  if (event->author->bot)
    return;

  struct discord_embed embed = {0};
  discord_embed_from_json(JSON, sizeof(JSON), &embed);
  embed.timestamp = discord_timestamp(client);

  struct discord_create_message params = {
      .content = "Test",
      .embeds =
          &(struct discord_embeds){
              .size = 1,
              .array = &embed,
          },
  };
  discord_create_message(client, event->channel_id, &params, NULL);

  discord_embed_cleanup(&embed);
}

int main(void) {
  struct discord *client = discord_config_init("concord/config.json");

  // ccord_global_init();

  discord_set_on_ready(client, &on_ready);
  discord_set_prefix(client, "+");
  discord_set_on_command(client, "embed", &on_dynamic);
  discord_run(client);

  discord_cleanup(client);
  // ccord_global_cleanup();
}
