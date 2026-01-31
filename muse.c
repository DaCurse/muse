#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "discord.h"
#include "transport.h"

#ifdef _WIN32
static const char *OS_NAME = "windows";
#else
static const char *OS_NAME = "linux";
#endif

static int test_break = 0;
/**
 * https://discord.com/developers/docs/events/gateway#list-of-intents
 * (1 << 0)  - GUILDS
 * (1 << 9)  - GUILD_MESSAGES
 * (1 << 15) - MESSAGE_CONTENT
 */
const uint32_t DISCORD_BOT_INTENTS = (1 << 0) | (1 << 9) | (1 << 15);

void test_on_connect(MuseTransport *ts) {
    (void)ts;

    printf("WebSocket connected!\n");
}

void test_on_message(MuseTransport *ts, const uint8_t *data, size_t length) {
    (void)ts;

    printf("Received %zu bytes long message: %.*s\n", length, (int)length,
           data);
    GatewayEventPayload payload = {0};
    if (!gateway_event_parse((uint8_t *)data, length, &payload)) {
        fprintf(stderr, "Failed to parse gateway event payload\n");
        return;
    }

    switch (payload.op) {
    case RECEIVE_OPCODE_HELLO:
        break;
    case RECEIVE_OPCODE_HEARTBEAT_ACK:
        break;
    case RECEIVE_OPCODE_HEARTBEAT:
        break;
    case RECEIVE_OPCODE_INVALID_SESSION:
        break;
    case RECEIVE_OPCODE_DISPATCH:
        break;
    }

    gateway_event_cleanup(&payload);
}

int main() {
    MuseTransport ts = {0};
    transport_init(&ts, NULL);

    transport_ws_open(&ts, "wss://gateway.discord.gg/?v=10&encoding=json",
                      (WSCallbacks){
                          .on_connect = test_on_connect,
                          .on_message = test_on_message,
                      });

    while (transport_still_running(&ts) && !test_break) {
        transport_poll(&ts, 1000L);
    }

    transport_destroy(&ts);
    return 0;
}

