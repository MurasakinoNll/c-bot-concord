#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/error.h>
#include <concord/interaction.h>
#include <concord/log.h>
#include <concord/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "channelutils.h"
#define VCHANNEL 1155156372338524351
#define ICON_URL "https://github.com/MurasakinoNll/c-bot-concord/blob/main/Cf.png?raw=true"

char JSONT[] =
    "[\n"
    "    {\n"
    "        \"type\": 1,\n"
    "        \"components\": [\n"
    "            {\n"
    "                \"type\": 2,\n"
    "                \"style\": 1,\n"
    "                \"label\": \"Press to create ticket\",\n"
    "                \"custom_id\": \"create_ticket\",\n"
    "                \"emoji\": {\n"
    "                    \"name\": \"\\uD83C\\uDF9F\\uFE0F\"\n"
    "                }\n"
    "            }\n"
    "        ]\n"
    "    }\n"
    "]\n";

char JSON[] = "{\n"
              "  \"title\": \"garak title\",\n"
              "  \"description\": \"testing desc\", \n"
              "  \"url\": \"http://127.0.0.1:8080/\", \n"
              "  \"color\" 3446043, \n"
              "}";


#define VICON_URL "https://media.discordapp.net/attachments/1171017496220938352/1263918534426365952/Confluence_-_Final_outlined.png?ex=6a7f57dd&is=6a7e065d&hm=215bd1246fdf4cdd468408402e41602589034bcc9ed0b8a1befbf836d8ff23e5&format=webp&quality=lossless&width=960&height=960&"

char JSONV[] = "{\n"
              "  \"title\": \"The Confluence\",\n"
              "  \"description\": \"Kindly open a ticket by pressing the button"
              "in order to be verified and to gain access to the rest "
              "of the server!\",\n"
              "  \"color\": 7100671,\n"
              "  \"footer\": {\n"
              "    \"text\": \"Confluence Bot by harak\",\n"
              "    \"icon_url\": \"" ICON_URL "\"\n"
              "  },\n"
              "  \"thumbnail\": {\n"
              "    \"url\": \"" ICON_URL "\"\n"
              "  },\n"
              "  \"author\": {\n"
              "    \"name\": \"\\u200b\",\n"
              "    \"icon_url\": \"" ICON_URL "\"\n"
              "  }\n"
              "}";

//-------------------------------------------------------------------------------------------

void ticketinit(struct discord *client, const struct discord_message *event) {
  if (event->author->bot)
    return;

  struct discord_embed embed = {0};
  discord_embed_from_json(JSONV, sizeof(JSONV), &embed);
  embed.timestamp = discord_timestamp(client);
  
  struct discord_components components = { 0 };
  discord_components_from_json(JSONT, sizeof(JSONT), &components);


  struct discord_create_message params = {
      .content = "",
      .components = &components,
      .embeds =
          &(struct discord_embeds){
              .size = 1,
              .array = &embed,
          },
  };
  discord_create_message(client, VCHANNEL, &params, NULL);

  discord_embed_cleanup(&embed);
  discord_components_cleanup(&components);
}

void on_interaction_create(struct discord *client, const struct discord_interaction *event){

  
  log_info("Interaction %" PRIu64 " received\n", event->id);
  if (!event->data || !event->data->custom_id){ printf("event rejected]n");return;}
  if (event->type != DISCORD_INTERACTION_MESSAGE_COMPONENT){ printf("interaction component failed]n");return;}

  if (0 == strncmp(event->data->custom_id, "create_ticket", 13)) {
    channel_create(client, event);
  }
  else{
    fprintf(stderr, "signal mismatch, received: [%s]\n", event->data->custom_id);
  }
}


//-------------------------------------------------------------------------------------------
//
void welcome_message(struct discord *client, u64snowflake targetchannel){
  
  struct discord_embed embed = {
  .color = 0x710067,
  };
  discord_embed_set_title(&embed, "C oonfluence");
  discord_embed_set_description( &embed, "bla bla bla\n bla");
  discord_embed_set_url(&embed, "https://github.com/MurasakinoNll/c-bot-concord");
  discord_embed_set_footer(&embed, "github.com/MurasakinoNll/c-bot-concord", ICON_URL,
                           NULL);
  discord_embed_set_image(&embed, ICON_URL, NULL, 0, 0);
  discord_embed_set_author(&embed, "jarking it", "https://github.com/MurasakinoNll", NULL, NULL);
  
  discord_embed_add_field( &embed, "extra field1",
                          "blabla: "
                          "blabla?",
                          false);

  struct discord_create_message params = {
    .content = "<@&1155152569526669391>",
    .embeds = &(struct discord_embeds){.size=1, .array=&embed,},
  };

  discord_create_message(client, targetchannel, &params, NULL);
}
void json_builder(struct discord *client, const struct discord_message *event){
  if (event->author->bot) return;

  struct discord_embed embed = {
    .color = 0x000000,
    .timestamp = discord_timestamp(client),
  };

  discord_embed_set_title(&embed, "C oonfluence");
  discord_embed_set_description( &embed, "bla bla bla\n bla");
  discord_embed_set_url(&embed, "https://github.com/MurasakinoNll/c-bot-concord");
  discord_embed_set_footer(&embed, "github.com/MurasakinoNll/c-bot-concord", ICON_URL,
                           NULL);
  discord_embed_set_image(&embed, ICON_URL, NULL, 0, 0);
  discord_embed_set_author(&embed, "jarking it", "https://github.com/MurasakinoNll", NULL, NULL);
  
  discord_embed_add_field( &embed, "extra field1",
                          "blabla: "
                          "blabla?",
                          false);

  struct discord_create_message params = {
    
    .embeds = 
      &(struct discord_embeds){.size=1, .array=&embed,},
  };
  discord_create_message(client, event->channel_id, &params, NULL);
  discord_embed_cleanup(&embed);
}

