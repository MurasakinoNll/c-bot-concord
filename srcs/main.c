#include "channelutils.h"
#include "customcom.h"
#include "help.h"
#include "mod.h"
#include "msglimit.h"
#include "ping.h"
#include "ptyshell.h"
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
#include <stdlib.h>
#include <time.h>

#include "msglimit.h"

#define GUARDED(alias, fn)                                                     \
  static void alias##_guarded(struct discord *client,                          \
                              const struct discord_message *event) {           \
    if (msglimit_enforce(client, event))                                       \
      return;                                                                  \
    fn(client, event);                                                         \
  }

GUARDED(helper, helper)
GUARDED(ticketinit, ticketinit)
GUARDED(json_builder, json_builder)
GUARDED(role_create, role_create)
GUARDED(role_delete, role_delete)
GUARDED(role_member_add, role_member_add)
GUARDED(role_member_remove, role_member_remove)
GUARDED(verify, verify)
GUARDED(cocverify, cocverify)
GUARDED(close_ticket, close_ticket)
GUARDED(cc_dispatch, cc_dispatch)

GUARDED(ban, ban)
GUARDED(mute, mute)
GUARDED(unban, unban)
GUARDED(unmute, unmute)

GUARDED(dungeon, dungeon)
GUARDED(undungeon, undungeon)
GUARDED(urban_command, urban_command)
GUARDED(temp_command, temp_command)
GUARDED(ping_command, ping_command)

GUARDED(ptystart_command, ptystart_command)
GUARDED(ptystop_command, ptystop_command)

static struct discord *g_client;

static void handle_sigint(int sig) {
  (void)sig;
  discord_shutdown(g_client);
}

#define PTY_CHANNEL_ID 1486807756290785400ULL

u64snowflake g_app_id;
#define ICON_URL                                                               \
  "https://github.com/MurasakinoNll/c-bot-concord/blob/main/Cf.png?raw=true"

void on_ready(struct discord *client, const struct discord_ready *event) {
  log_info("main bot connected to discord as %s#%s", event->user->username,
           event->user->discriminator);
  g_app_id = event->application->id;
}

void on_message_fallback(struct discord *client,
                         const struct discord_message *event) {
  if (msglimit_enforce(client, event))
    return;
  if (event->channel_id == PTY_CHANNEL_ID) {
    ptyinput_fallback(client, event);
    return;
  }
  cc_trigger_check(client, event);
}

int main(void) {

  setenv("TZ", "UTC", 1);
  tzset();

  struct discord *client = discord_config_init("concord/config.json");
  g_client = client;
  signal(SIGINT, handle_sigint);
  customcom_init();
  ticket_db_init();
  msglimit_db_init();

  discord_set_on_ready(client, &on_ready);
  discord_set_prefix(client, "+");

  discord_set_on_command(client, "help", &helper_guarded);
  discord_set_on_command(client, "embed", &ticketinit_guarded);
  discord_set_on_command(client, "builder", &json_builder_guarded);
  discord_set_on_command(client, "rolecreate", &role_create_guarded);
  discord_set_on_command(client, "roledelete", &role_delete_guarded);
  discord_set_on_command(client, "roleadd", &role_member_add_guarded);
  discord_set_on_command(client, "roleremove", &role_member_remove_guarded);
  discord_set_on_command(client, "v", &verify_guarded);
  discord_set_on_command(client, "verify", &verify_guarded);
  discord_set_on_command(client, "cocverify", &cocverify_guarded);
  discord_set_on_command(client, "close", &close_ticket_guarded);
  discord_set_on_command(client, "cc", &cc_dispatch_guarded);

  discord_set_on_command(client, "ban", &ban_guarded);
  discord_set_on_command(client, "mute", &mute_guarded);
  discord_set_on_command(client, "unban", &unban_guarded);
  discord_set_on_command(client, "unmute", &unmute_guarded);
  discord_set_on_command(client, "msglimit", &msglimit_command);
  discord_set_on_message_update(client, &msglimit_enforce_edit);

  discord_set_on_command(client, "dungeon", &dungeon_guarded);
  discord_set_on_command(client, "undungeon", &undungeon_guarded);
  discord_set_on_command(client, "urban", &urban_command_guarded);
  discord_set_on_command(client, "temp", &temp_command_guarded);
  discord_set_on_command(client, "ping", &ping_command_guarded);

  discord_set_on_command(client, "ptystart", &ptystart_command_guarded);
  discord_set_on_command(client, "ptystop", &ptystop_command_guarded);

  discord_set_on_message_create(client, &on_message_fallback);

  discord_set_on_interaction_create(client, &on_interaction_create);

  discord_run(client);

  discord_cleanup(client);
}
