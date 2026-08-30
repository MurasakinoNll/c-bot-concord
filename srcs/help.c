#include "help.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef struct { char *title; char *body; } help_page;

static const help_page pages[] = {
  {
    "__c ore__:",
    "**prefix** '+'\n\n"
    "**embed** call ticketinit() in verification channel\n"
    "**builder** json_builder callback\n"
  },
  {
  "__useful shit__:",
    "**urban** <query>\n"
  },
  {
    "__mod__:",
    "**ban** <uid|mention>\n"
    "**mute** <uid|mention> <duration[minutes]>\n"
    "**unban** <uid>\n"
    "**unmute** <uid|mention>\n"
  },
  {
    "__roleutil__:",
    "**rolecreate** <rolename>\n"
    "**roledelete** <role_id>\n"
    "**roleadd** <user_id> <role_id>\n"
    "**roleremove** <user_id> <role_id>\n"
  },
  {
    "__alias/shortcut__:",
    "**v|verify** <uid>\n"
    "**close** (closes ticket it is used in)\n"
  },
  {
    "__Custom Commands__:",
    "**cc** subcommands:\n"
    "    **create** <name> <response>\n"
    "    **delete** <name>\n"
    "    **list** <NULL>\n"
    "    **search** <query>\n"
    "    **cooldown** <name>\n"
  },
  {
    "misc:",
    "**help** this menu\n"
  },
};
#define HELP_PAGE_COUNT (int)(sizeof(pages)/sizeof(pages[0]))
#define HELP_PAGE_SIZE 3
#define HELP_TOTAL_PAGES ((HELP_PAGE_COUNT + HELP_PAGE_SIZE - 1) / HELP_PAGE_SIZE)

static void build_help_buttons(struct discord_component buttons[2], char prev_id[64], char next_id[64],
                                u64snowflake requester_id, int page) {
  snprintf(prev_id, 64, "helppg:p:%d:%" PRIu64, page, requester_id);
  snprintf(next_id, 64, "helppg:n:%d:%" PRIu64, page, requester_id);
  buttons[0] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_SECONDARY, .label = "◀", .custom_id = prev_id
  };
  buttons[1] = (struct discord_component){
    .type = DISCORD_COMPONENT_BUTTON, .style = DISCORD_BUTTON_SECONDARY, .label = "▶", .custom_id = next_id
  };
}

static void send_help_embed(struct discord *client, u64snowflake channel_id,
                             u64snowflake requester_id, int page, bool is_edit,
                             u64snowflake interaction_id, const char *interaction_token) {
  if (page < 0) page = 0;
  if (page >= HELP_TOTAL_PAGES) page = HELP_TOTAL_PAGES - 1;

  char footer[32];
  snprintf(footer, sizeof footer, "Page %d / %d", page + 1, HELP_TOTAL_PAGES);

  struct discord_embed embed = { .color = 0xFFFFFF };
  discord_embed_set_title(&embed, "Confluence help menu");
  discord_embed_set_url(&embed, "https://github.com/MurasakinoNll/c-bot-concord");
  discord_embed_set_footer(&embed, footer, NULL, NULL);

  int start = page * HELP_PAGE_SIZE;
  int end = start + HELP_PAGE_SIZE;
  if (end > HELP_PAGE_COUNT) end = HELP_PAGE_COUNT;

  for (int i = start; i < end; i++) {
    discord_embed_add_field(&embed, pages[i].title, pages[i].body, false);
  }

  char prev_id[64], next_id[64];
  struct discord_component buttons[2];
  build_help_buttons(buttons, prev_id, next_id, requester_id, page);
  struct discord_components button_list = { .size = 2, .array = buttons };
  struct discord_component action_row = {
    .type = DISCORD_COMPONENT_ACTION_ROW,
    .components = &button_list,
  };
  struct discord_components row_list = { .size = 1, .array = &action_row };

  if (is_edit) {
    struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_UPDATE_MESSAGE,
      .data = &(struct discord_interaction_callback_data){
        .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
        .components = &row_list,
      },
    };
    discord_create_interaction_response(client, interaction_id, interaction_token, &resp, NULL);
  } else {
    struct discord_create_message params = {
      .embeds = &(struct discord_embeds){ .size = 1, .array = &embed },
      .components = &row_list,
    };
    discord_create_message(client, channel_id, &params, NULL);
  }

  discord_embed_cleanup(&embed);
}
void helper(struct discord *client, const struct discord_message *event) {
  if (event->author->bot) return;
  send_help_embed(client, event->channel_id, event->author->id, 0, false, 0, NULL);
}

void help_handle_interaction(struct discord *client, const struct discord_interaction *event) {
  if (!event->data || !event->data->custom_id) return;
  if (strncmp(event->data->custom_id, "helppg:", 7) != 0) return;

  char action[4] = "";
  int page = 0;
  u64snowflake requester_id = 0;
  sscanf(event->data->custom_id, "helppg:%3[^:]:%d:%" SCNu64, action, &page, &requester_id);

  u64snowflake clicker_id = (event->member && event->member->user) ? event->member->user->id : 0;
  if (clicker_id != requester_id) {
    struct discord_interaction_response resp = {
      .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
      .data = &(struct discord_interaction_callback_data){
        .content = "only the person who ran this command can use these buttons.",
        .flags = DISCORD_MESSAGE_EPHEMERAL,
      },
    };
    discord_create_interaction_response(client, event->id, event->token, &resp, NULL);
    return;
  }

  if (strcmp(action, "n") == 0) page++;
  else if (strcmp(action, "p") == 0) page--;

  send_help_embed(client, event->channel_id, requester_id, page, true, event->id, event->token);
}
