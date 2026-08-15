#include <assert.h>
#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <stdbool.h>
#include "ticketsystem.h"
u64snowflake g_app_id;
#define ICON_URL "https://github.com/MurasakinoNll/c-bot-concord/blob/main/Cf.png?raw=true"
void on_ready(struct discord *client, const struct discord_ready *event) {
  log_info("main bot connected to discord as %s#%s", event->user->username,
           event->user->discriminator);
  g_app_id = event->application->id;
}

int main(void) {
  struct discord *client = discord_config_init("concord/config.json");


  discord_set_on_ready(client, &on_ready);
  discord_set_prefix(client, "+");
  discord_set_on_command(client, "embed", &ticketinit);
  discord_set_on_command(client, "builder", &json_builder);
  discord_set_on_interaction_create(client, &on_interaction_create);


  discord_run(client);

  discord_cleanup(client);
}
