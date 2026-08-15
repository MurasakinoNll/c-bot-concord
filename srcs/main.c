#include <assert.h>
#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <concord/types.h>
#include <locale.h>
#include <stdbool.h>
#include "roleutils.h"
#include "ticketsystem.h"
u64snowflake g_app_id;
#define ICON_URL "https://github.com/MurasakinoNll/c-bot-concord/blob/main/Cf.png?raw=true"
void on_ready(struct discord *client, const struct discord_ready *event) {
  log_info("main bot connected to discord as %s#%s", event->user->username,
           event->user->discriminator);
  g_app_id = event->application->id;
}

void helper(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  struct discord_embed embed = {.color = 0xFFFFFF, .timestamp=discord_timestamp(client),};
  
  discord_embed_set_title(&embed, "C onfluence help menu");
  discord_embed_set_url(&embed, "https://github.com/MurasakinoNll/c-bot-concord");
  discord_embed_add_field(&embed, "Core: \n",

                          "**prefix** '+'\n\n"
                          "**embed** call ticketinit() in verification channel\n"
                          "**builder** json_builder callback\n"
                          "**rolecreate** <rolename>\n"
                          "**roledelete** <role_id>\n"
                          "**roleadd** <user_id> <role_id>\n"
                          "**roleremove** <user_id> <role_id>\n"
                          "**help** this menu\n"

                          ,false);
  
  struct discord_create_message params = {
    .embeds = &(struct discord_embeds){.size=1, .array=&embed,},
  } ;
  discord_create_message(client, event->channel_id, &params, NULL);
  discord_embed_cleanup(&embed);
}

int main(void) {
  struct discord *client = discord_config_init("concord/config.json");


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
  discord_set_on_interaction_create(client, &on_interaction_create);


  discord_run(client);

  discord_cleanup(client);
}
