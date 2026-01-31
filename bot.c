#include "bot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define OS_NAME ("windows")
#else
#define OS_NAME ("linux")
#endif

static uint64_t get_now_ms(void) {
#ifndef _WIN32
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#else
    return (uint64_t)GetTickCount64();
#endif
}

void bot_init(MuseBot *bot, MuseTransport *ts, const char *token,
              int32_t intents, BotEventCallbacks callbacks) {
    bot->ts = ts;
    bot->token = token;
    bot->intents = intents;
    bot->callbacks = callbacks;
    bot->is_running = true;
    bot->is_connected = false;
    bot->last_seq = -1;
}

void bot_set_gateway_url(MuseBot *bot, const char *url) {
    strncpy(bot->gateway_url, url, sizeof(bot->gateway_url) - 1);
    bot->gateway_url[sizeof(bot->gateway_url) - 1] = '\0';
}

static void bot_send_heartbeat(MuseBot *bot) {
    MuseTransport *ts = bot->ts;
    cJSON *heartbeat_json = gateway_event_heartbeat(bot->last_seq);
    transport_ws_send_json(ts, heartbeat_json);
    cJSON_Delete(heartbeat_json);
    printf("Sent HEARTBEAT with seq %d\n", bot->last_seq);
}

static void bot_ensure_connection(MuseBot *bot) {
    if (!transport_is_ws_open(bot->ts)) {
        uint64_t now = get_now_ms();

        if (now - bot->last_reconnect_attempt > RECONNECT_INTERVAL_MS) {
            if (bot->last_reconnect_attempt != 0) {
                printf("Transport down. Reconnecting...\n");
            } else {
                printf("Establishing initial connection...\n");
            }
            transport_ws_open(bot->ts, bot->gateway_url);
            bot->last_reconnect_attempt = now;

            // Reset heartbeat logic on new connection
            bot->heartbeat_interval_ms = 0;
        }
    }
}

static void bot_ensure_heartbeat(MuseBot *bot) {
    if (bot->heartbeat_interval_ms > 0 && transport_is_ws_open(bot->ts)) {
        uint64_t now = get_now_ms();
        if (now >= bot->next_heartbeat_ms) {
            bot_send_heartbeat(bot);
            bot->next_heartbeat_ms = now + bot->heartbeat_interval_ms;
        }
    }
}

void bot_tick(MuseBot *bot) {
    if (!bot || !bot->ts)
        return;

    bot_ensure_connection(bot);
    transport_poll(bot->ts, POLL_TIMEOUT_MS);
    bot_ensure_heartbeat(bot);
}

static void bot_send_identify(MuseBot *bot) {
    MuseTransport *ts = bot->ts;
    IdentifyEventData identify_data = {
        .token = bot->token,
        .properties =
            {
                .os = OS_NAME,
                .browser = "muse",
                .device = "muse",
            },
        .intents = bot->intents,
    };

    cJSON *identify_json = gateway_event_identify(&identify_data);
    transport_ws_send_json(ts, identify_json);
    cJSON_Delete(identify_json);
    printf("Sent IDENTIFY\n");
}

static void bot_send_resume(MuseBot *bot) {
    MuseTransport *ts = bot->ts;
    cJSON *resume_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(resume_json, "op", SEND_OPCODE_RESUME);

    cJSON *d_json = cJSON_CreateObject();
    cJSON_AddStringToObject(d_json, "token", bot->token);
    cJSON_AddStringToObject(d_json, "session_id", bot->session_id);
    cJSON_AddNumberToObject(d_json, "seq", bot->last_seq);
    cJSON_AddItemToObject(resume_json, "d", d_json);

    transport_ws_send_json(ts, resume_json);
    cJSON_Delete(resume_json);
    printf("Sent RESUME\n");
}

static void bot_handle_hello(MuseBot *bot, const cJSON *data_json) {
    HelloEventData hello_data = {0};
    if (!gateway_event_parse_hello(data_json, &hello_data)) {
        fprintf(stderr, "Failed to parse HELLO event data\n");
        return;
    }

    printf("Received HELLO, heartbeat interval: %d ms\n",
           hello_data.heartbeat_interval);
    bot->heartbeat_interval_ms = hello_data.heartbeat_interval;
    bot->next_heartbeat_ms = get_now_ms() + bot->heartbeat_interval_ms;
    bot_send_heartbeat(bot);

    if (bot->session_id == NULL) {
        bot_send_identify(bot);
    } else {
        bot_send_resume(bot);
    }
}

static void bot_handle_invalid_session(MuseBot *bot, const cJSON *data_json) {
    bool resumable = data_json && cJSON_IsTrue(data_json);
    printf("Received INVALID_SESSION (Resumable: %s)\n",
           resumable ? "true" : "false");

    if (resumable && bot->session_id != NULL) {
        bot_send_resume(bot);
    } else {
        if (bot->session_id == NULL) {
            fprintf(stderr, "FATAL: Gateway rejected initial connection. Check "
                            "TOKEN/INTENTS.\n");
            bot->is_running = false;
            transport_ws_close(bot->ts);
        } else {
            printf("Session expired. Clearing state for clean Identify...\n");
            free(bot->session_id);
            bot->session_id = NULL;
            bot->last_seq = -1;
            transport_ws_close(bot->ts);
        }
    }
}

static void bot_handle_ready(MuseBot *bot, const cJSON *data_json) {
    cJSON *session_id_json = cJSON_GetObjectItem(data_json, "session_id");
    if (!session_id_json)
        return;

    if (bot->session_id)
        free(bot->session_id);
    bot->session_id = strdup(session_id_json->valuestring);
    printf("Bot READY. Session ID: %s\n", bot->session_id);

    cJSON *gateway_url_json =
        cJSON_GetObjectItem(data_json, "resume_gateway_url");
    if (gateway_url_json && cJSON_IsString(gateway_url_json)) {
        bot_set_gateway_url(bot, gateway_url_json->valuestring);
        printf("Updated gateway URL for resuming: %s\n", bot->gateway_url);
    }
}

static void handle_dispatch_event(MuseBot *bot, const char *event_name,
                                  const cJSON *data_json) {
    if (strcmp(event_name, "READY") == 0) {
        bot_handle_ready(bot, data_json);
        if (bot->callbacks.on_ready) {
            bot->callbacks.on_ready(bot, event_name, data_json);
        }

    } else if (strcmp(event_name, "MESSAGE_CREATE") == 0) {
        if (bot->callbacks.on_message_create) {
            bot->callbacks.on_message_create(bot, event_name, data_json);
        }
    } else {
        printf("Unhandled DISPATCH event: %s\n", event_name);
    }
}

void bot_handle_gateway_event(MuseBot *bot,
                              const GatewayEventPayload *payload) {
    if (payload->s != -1)
        bot->last_seq = payload->s;

    switch (payload->op) {
    case RECEIVE_OPCODE_HELLO:
        bot_handle_hello(bot, payload->d_json);
        break;
    case RECEIVE_OPCODE_HEARTBEAT_ACK:
        printf("Heartbeat ACK.\n");
        break;
    case RECEIVE_OPCODE_HEARTBEAT:
        bot_send_heartbeat(bot);
        break;
    case RECEIVE_OPCODE_RECONNECT:
        printf("Discord requested RECONNECT.\n");
        transport_ws_close(bot->ts);
        break;
    case RECEIVE_OPCODE_INVALID_SESSION:
        bot_handle_invalid_session(bot, payload->d_json);
        break;
    case RECEIVE_OPCODE_DISPATCH:
        handle_dispatch_event(bot, payload->t, payload->d_json);
        break;
    default:
        printf("Unhandled gateway opcode: %d\n", payload->op);
        break;
    }
}

void bot_destroy(MuseBot *bot) {
    if (bot->session_id) {
        free(bot->session_id);
        bot->session_id = NULL;
    }
}
