#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "bot.h"
#include "discord.h"
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

void on_bot_message_create(MuseBot *bot, const char *event_name,
                           const cJSON *data_json) {
    const cJSON *content_json =
        cJSON_GetObjectItemCaseSensitive(data_json, "content");
    const cJSON *author_json =
        cJSON_GetObjectItemCaseSensitive(data_json, "author");
    const cJSON *username_json =
        cJSON_GetObjectItemCaseSensitive(author_json, "username");

    if (cJSON_IsString(content_json) && cJSON_IsString(username_json)) {
        printf("[%s] [%s]: %s\n", event_name, username_json->valuestring,
               content_json->valuestring);
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
