#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "bot.h"
#include "discord.h"
#include "links.h"
#include "transport.h"

#define USER_AGENT ("Muse (https://github.com/DaCurse/muse, 1.0)")
#define GATEWAY_URL ("wss://gateway.discord.gg/?v=10&encoding=json")

/**
 * GUILDS, GUILD_MESSAGES, MESSAGE_CONTENT
 * https://discord.com/developers/docs/events/gateway#list-of-intents
 */
const uint32_t DISCORD_BOT_INTENTS = (1 << 0) | (1 << 9) | (1 << 15);

void on_connect(MuseTransport *ts) {
    MuseBot *bot = (MuseBot *)ts->user_data;
    bot->is_connected = true;
    printf("WebSocket connected!\n");
}

void on_disconnect(MuseTransport *ts) {
    MuseBot *bot = (MuseBot *)ts->user_data;
    bot->is_connected = false;
    printf("WebSocket disconnected.\n");
}

typedef struct {
    MuseBot *bot;
    char *channel_id;
} MusicLinkContext;

void on_music_link_fetched(HTTPResponse *res, void *user_data) {
    MusicLinkContext *ctx = (MusicLinkContext *)user_data;
    MuseBot *bot = ctx->bot;
    const char *channel_id = ctx->channel_id;

    if (res->result != CURLE_OK) {
        fprintf(stderr, "Failed to fetch music links: %s\n",
                curl_easy_strerror(res->result));
        return;
    }

    cJSON *json = cJSON_ParseWithLength((const char *)res->data, res->length);
    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "Failed to parse music links JSON: %s\n",
                    error_ptr);
        }
        return;
    }

    MusicLinks links = {0};
    parse_music_links_response(json, &links);
    cJSON_Delete(json);

    DiscordEmbedField fields[3] = {0};
    int field_count = 0;

    if (links.spotify_url) {
        fields[field_count].name = "Spotify";
        fields[field_count].value = links.spotify_url;
        fields[field_count].inline_field = false;
        field_count++;
    }
    if (links.youtube_url) {
        fields[field_count].name = "YouTube";
        fields[field_count].value = links.youtube_url;
        fields[field_count].inline_field = false;
        field_count++;
    }
    if (links.apple_music_url) {
        fields[field_count].name = "Apple Music";
        fields[field_count].value = links.apple_music_url;
        fields[field_count].inline_field = false;
        field_count++;
    }

    DiscordEmbedImage thumbnail = {0};
    if (links.thumbnail_url) {
        thumbnail.url = links.thumbnail_url;
    }

    DiscordEmbed embed = {
        .title = "Music Links",
        .type = "rich",
        .description = "Here are the available music links:",
        .color = 0x35556e,
        .thumbnail = thumbnail,
        .image = NULL,
        .fields = {{0}},
    };

    for (int i = 0; i < field_count; ++i) {
        embed.fields[i] = fields[i];
    }

    DiscordCreateMessage message = {
        .content = "",
        .nonce = (int32_t)time(NULL),
        .embeds = {embed},
    };

    bot_rest_send_message(bot, channel_id, &message);

    music_links_free(&links);
    free(ctx->channel_id);
    free(ctx);
}

void on_bot_message_create(MuseBot *bot, const char *event_name,
                           const cJSON *data_json) {
    (void)event_name;

    const cJSON *author_json =
        cJSON_GetObjectItemCaseSensitive(data_json, "author");

    const cJSON *author_id_json =
        cJSON_GetObjectItemCaseSensitive(author_json, "id");
    if (!cJSON_IsString(author_id_json))
        return;

    const char *author_id = author_id_json->valuestring;
    if (strcmp(author_id, bot->user_id) == 0) {
        return;
    }

    const cJSON *content_json =
        cJSON_GetObjectItemCaseSensitive(data_json, "content");
    const cJSON *channel_id_json =
        cJSON_GetObjectItemCaseSensitive(data_json, "channel_id");

    if (!cJSON_IsString(content_json) || !cJSON_IsString(channel_id_json))
        return;

    const char *content = content_json->valuestring;
    const char *channel_id = channel_id_json->valuestring;

    char *music_url = NULL;
    if (is_music_link(content, &music_url)) {
        printf("Detected music link '%s' in channel %s by user %s\n", music_url,
               channel_id, author_id_json->valuestring);
        MusicLinkContext *ctx =
            (MusicLinkContext *)malloc(sizeof(MusicLinkContext));
        ctx->bot = bot;
        ctx->channel_id = strdup(channel_id);
        fetch_music_links(bot->ts, music_url, on_music_link_fetched, ctx);
        free(music_url);
    }
}

void on_message(MuseTransport *ts, const uint8_t *data, size_t length) {
    MuseBot *bot = (MuseBot *)ts->user_data;

    GatewayEventPayload payload = {0};
    if (!gateway_event_parse((uint8_t *)data, length, &payload)) {
        fprintf(stderr, "Failed to parse gateway event payload\n");
        return;
    }

    bot_handle_gateway_event(bot, &payload);

    gateway_event_cleanup(&payload);
}

static volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\nSignal received. Shutting down gracefully...\n");
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (getenv("TOKEN") == NULL) {
        fprintf(stderr, "Error: TOKEN environment variable not set.\n");
        return 1;
    }

    MuseTransport ts = {0};
    MuseBot bot = {0};
    BotEventCallbacks callbacks = {
        .on_ready = NULL,
        .on_message_create = on_bot_message_create,
    };
    bot_init(&bot, &ts, getenv("TOKEN"), DISCORD_BOT_INTENTS, callbacks);
    bot_set_gateway_url(&bot, GATEWAY_URL);

    WSCallbacks cbs = {
        .on_connect = on_connect,
        .on_disconnect = on_disconnect,
        .on_message = on_message,
    };
    transport_init(&ts, USER_AGENT, cbs, &bot);

    while (keep_running && bot.is_running) {
        bot_tick(&bot);
    }

    printf("Exiting...\n");
    bot_destroy(&bot);
    transport_destroy(&ts);

    return 0;
}
