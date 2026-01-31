#ifndef BOT_H
#define BOT_H

#include <stdbool.h>
#include <stdint.h>

#include <cjson/cJSON.h>

#include "discord.h"
#include "transport.h"

#define POLL_TIMEOUT_MS (100L)
#define RECONNECT_INTERVAL_MS (5000L)

typedef struct MuseBot MuseBot;

typedef void (*DispatchEventHandler)(MuseBot *bot, const char *event_name,
                                     const cJSON *data_json);

typedef struct {
    DispatchEventHandler on_ready;
    DispatchEventHandler on_message_create;
} BotEventCallbacks;

typedef struct MuseBot {
    MuseTransport *ts;
    char gateway_url[256];
    const char *token;
    int32_t intents;

    uint64_t last_reconnect_attempt;
    bool is_running;
    bool is_connected;
    char *session_id;

    int32_t last_seq;
    int32_t heartbeat_interval_ms;
    uint64_t next_heartbeat_ms;

    BotEventCallbacks callbacks;
} MuseBot;

void bot_init(MuseBot *bot, MuseTransport *ts, const char *token,
              int32_t intents, BotEventCallbacks callbacks);
void bot_set_gateway_url(MuseBot *bot, const char *url);
void bot_tick(MuseBot *bot);

// returns True if the event was DISPATCH, letting the caller handle it
void bot_handle_gateway_event(MuseBot *bot, const GatewayEventPayload *payload);

void bot_destroy(MuseBot *bot);

#endif // BOT_H
