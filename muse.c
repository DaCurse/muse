#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "bot.h"
#include "cache.h"
#include "discord.h"
#include "links.h"
#include "transport.h"

#define USER_AGENT ("Muse (https://github.com/DaCurse/muse, 1.0)")
#define GATEWAY_URL ("wss://gateway.discord.gg/?v=10&encoding=json")

#define CMD_CACHE_SUMMARY ";cachesummary"
#define CMD_CACHE_SUMMARY_BUFFER_SIZE 2000

#ifdef _WIN32
#include <io.h>
#define ISATTY _isatty
#define FILENO _fileno
#else
#include <unistd.h>
#define ISATTY isatty
#define FILENO fileno
#endif

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

void on_music_link_fetched(MusicLinks links, void *user_data) {
    static const char *platform_names[] = {
        [PLATFORM_SPOTIFY] = "Spotify",
        [PLATFORM_YOUTUBE] = "YouTube",
        [PLATFORM_APPLE_MUSIC] = "Apple Music",
        [PLATFORM_TIDAL] = "Tidal",
        [PLATFORM_SOUNDCLOUD] = "SoundCloud",
    };

    MusicLinkContext *ctx = (MusicLinkContext *)user_data;
    MuseBot *bot = ctx->bot;
    const char *channel_id = ctx->channel_id;

    DiscordEmbedField fields[PLATFORM_COUNT] = {0};
    int field_count = 0;

    for (size_t i = 0; i < PLATFORM_COUNT; i++) {
        const char *url = links.data->urls[i];
        if (url && links.original_platform != i) {
            fields[field_count].name = platform_names[i];
            fields[field_count].value = url;
            fields[field_count].inline_field = false;
            field_count++;
        }
    }

    DiscordEmbedImage thumbnail = {0};
    if (links.data->thumbnail_url) {
        thumbnail.url = links.data->thumbnail_url;
    }

    DiscordEmbed embed = {0};
    if (field_count > 0) {
        embed.title = "Music Links";
        embed.type = "rich";
        embed.description = "Here are the music links I found on other platforms:";
        embed.color = 0x35556e;
        embed.thumbnail = thumbnail;

        for (int i = 0; i < field_count; i++) {
            embed.fields[i] = fields[i];
        }

    } else if (links.original_platform != PLATFORM_YOUTUBE) {
        // Don't send an error for youtube since links are likely to not be
        // songs
        embed.title = "Music Links";
        embed.type = "rich";
        embed.description = "I couldn't find any music links for "
                            "this track on other platforms :pensive:";
        embed.color = 0xc8393e;
    }

    if (embed.title) {
        DiscordCreateMessage message = {
            .content = "",
            .nonce = time(NULL),
            .embeds = {embed},
        };
        bot_rest_send_message(bot, channel_id, &message);
    } else {
        printf("YouTube link with no results detected, skipping sending "
               "message.\n");
    }

    free(ctx->channel_id);
    free(ctx);
}

void handle_music_link(MuseBot *bot, const char *channel_id, const char *music_url) {

    MusicLinkContext *ctx = (MusicLinkContext *)malloc(sizeof(MusicLinkContext));
    if (!ctx) {
        fprintf(stderr, "Failed to allocate music link context");
        return;
    }
    ctx->bot = bot;
    ctx->channel_id = strdup(channel_id);
    if (!ctx->channel_id) {
        fprintf(stderr, "Failed to duplicate channel ID\n");
        goto cleanup_ctx;
    }
    if (!fetch_music_links(bot->ts, music_url, on_music_link_fetched, ctx)) {
        // We got rate limited, callback won't fire, so free the context
        goto cleanup_channel_id;
    }
    return;

cleanup_channel_id:
    free(ctx->channel_id);
cleanup_ctx:
    free(ctx);
}

void on_bot_message_create(MuseBot *bot, const char *event_name, const cJSON *data_json) {
    (void)event_name;

    // TODO: Bot should parse this for us and pass a struct in the callback
    const cJSON *author_json = cJSON_GetObjectItemCaseSensitive(data_json, "author");

    const cJSON *author_id_json = cJSON_GetObjectItemCaseSensitive(author_json, "id");
    if (!cJSON_IsString(author_id_json))
        return;

    const char *author_id = author_id_json->valuestring;
    if (strcmp(author_id, bot->user_id) == 0) {
        return;
    }

    const cJSON *content_json = cJSON_GetObjectItemCaseSensitive(data_json, "content");
    const cJSON *channel_id_json = cJSON_GetObjectItemCaseSensitive(data_json, "channel_id");

    if (!cJSON_IsString(content_json) || !cJSON_IsString(channel_id_json))
        return;

    const char *content = content_json->valuestring;
    const char *channel_id = channel_id_json->valuestring;

    char *music_url = NULL;
    if (is_music_link(content, NULL, &music_url)) {
        printf("Detected music link '%s' in channel %s by user %s\n",
               music_url,
               channel_id,
               author_id_json->valuestring);
        handle_music_link(bot, channel_id, music_url);
        free(music_url);
    } else if (strlen(content) >= strlen(CMD_CACHE_SUMMARY) &&
               strncmp(content, CMD_CACHE_SUMMARY, strlen(CMD_CACHE_SUMMARY)) == 0) {
        char *summary_buffer = calloc(CMD_CACHE_SUMMARY_BUFFER_SIZE, sizeof(*summary_buffer));
        if (!summary_buffer) {
            fprintf(stderr, "Failed to allocate cache summary buffer\n");
            return;
        }
        cache_summary(summary_buffer, CMD_CACHE_SUMMARY_BUFFER_SIZE);

        DiscordCreateMessage message = {
            .content = summary_buffer,
            .nonce = time(NULL),
        };
        bot_rest_send_message(bot, channel_id, &message);
        free(summary_buffer);
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
    if (!links_init()) {
        fprintf(stderr, "Error: failed to compile regular expressions");
        return 1;
    }

    if (!ISATTY(FILENO(stdout))) {
        setvbuf(stdout, NULL, _IOLBF, 0);
    }

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
    cache_destroy();
    links_destroy();

    return 0;
}
