#include <concord/discord.h>
#include <concord/discord_codecs.h>
#include <concord/log.h>
#include <stdio.h>
#include <string.h>
#include "ticketsystem.h"
void channel_create(struct discord *client, const struct discord_interaction *event){
    if (!event->member || !event->member->user || !event->member->user->username) {
        log_error("Interaction event user profile is missing data structure profiles.");
        return;
    }
  int tc = load_ticketcounter();
    char name_buffer[256] = {0};
    snprintf(name_buffer, sizeof(name_buffer), "Ticket-%s-%i", event->member->user->username, tc);

    struct discord_interaction_callback_data response_data = {
        .content = "Your ticket has been created :white_check_mark:",
        .flags = DISCORD_MESSAGE_EPHEMERAL
    };

    struct discord_interaction_response response = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &response_data
    };

    discord_create_interaction_response(client, event->id, event->token, &response, NULL);

    struct discord_overwrite overwrites[] = {
        {
            .id = 1155152569040130101ULL,
            .type = 0,
            .allow = 0ULL,
            .deny = 1ULL << 10
        },
        {
            .id = 1155152569526669391ULL,
            .type = 0,
            .allow = (1ULL << 10) | (1ULL << 11),
            .deny = 0ULL
        },
        {
            .id = event->member->user->id,
            .type = 1,
            .allow = (1ULL << 10) | (1ULL << 11) | (1ULL << 16),
            .deny = 0ULL
        }
    };

    struct discord_create_guild_channel params = {
        .name = name_buffer,
        .type = 0,
        .parent_id = 1536042058903720058ULL,
        .permission_overwrites = &(struct discord_overwrites){
            .array = overwrites,
            .size = 3
        }
    };
    struct discord_channel new_channel = {0};
    struct discord_ret_channel ret = {
      .sync = &new_channel
    };
  
    CCORDcode channel_status = discord_create_guild_channel(client, event->guild_id, &params, &ret);
    
    if (channel_status != CCORD_OK) {
        log_error("Guild channel generation failed with error code: %d", channel_status);
    } else {
        welcome_message(client, new_channel.id);
        log_info("Ticket channel successfully deployed via layout engine.");
    }
}
