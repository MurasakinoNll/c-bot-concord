#include "channelutils.h"
#include "customcom.h"
#include "help.h"
#include "mod.h"
#include "roleutils.h"
#include "temp.h"
#include "ticketsystem.h"
#include "urban.h"
#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include "ping.h"
#include "msglimit.h"


static struct discord *g_client;

static void handle_sigint(int sig) {
  (void)sig;
  discord_shutdown(g_client);
}

u64snowflake g_app_id;
#define ICON_URL                                                               \
  "https://github.com/MurasakinoNll/c-bot-concord/blob/main/Cf.png?raw=true"

void on_ready(struct discord *client, const struct discord_ready *event) {
  log_info("main bot connected to discord as %s#%s", event->user->username,
           event->user->discriminator);
  g_app_id = event->application->id;
}

void on_message_fallback(struct discord *client, const struct discord_message *event){
  if(msglimit_enforce(client, event)) return;
  cc_trigger_check(client, event);
}

int main(void) {
  struct discord *client = discord_config_init("concord/config.json");
  g_client = client;
  signal(SIGINT, handle_sigint);
  customcom_init();
  ticket_db_init();
  msglimit_db_init();

  discord_set_on_ready(client, &on_ready);
  discord_set_prefix(client, "+");
  discord_set_on_command(client, "help", &helper);
  discord_set_on_command(client, "embed", &ticketinit);
  discord_set_on_command(client, "builder", &json_builder);
  discord_set_on_command(client, "rolecreate", &role_create);
  discord_set_on_command(client, "roledelete", &role_delete);
  discord_set_on_command(client, "roleadd", &role_member_add);
  discord_set_on_command(client, "roleremove", &role_member_remove);
  discord_set_on_command(client, "v", &verify);
  discord_set_on_command(client, "verify", &verify);
  discord_set_on_command(client, "close", &close_ticket);
  discord_set_on_command(client, "cc", &cc_dispatch);

  discord_set_on_command(client, "ban", &ban);
  discord_set_on_command(client, "mute", &mute);
  discord_set_on_command(client, "unban", &unban);
  discord_set_on_command(client, "unmute", &unmute);
  discord_set_on_command(client, "msglimit", &msglimit_command);

  discord_set_on_command(client, "urban", &urban_command);
  discord_set_on_command(client, "temp", &temp_command);
  discord_set_on_command(client, "ping", &ping_command);

  discord_set_on_message_create(client, &on_message_fallback);

  discord_set_on_interaction_create(client, &on_interaction_create);

  discord_run(client);

  discord_cleanup(client);
}
