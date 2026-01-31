#ifndef DISCORD_H
#define DISCORD_H

#include <cjson/cJSON.h>
#include <stdbool.h>
#include <stdint.h>

// https://discord.com/developers/docs/topics/opcodes-and-status-codes#gateway-gateway-opcodes
typedef enum {
    RECEIVE_OPCODE_DISPATCH = 0,
    RECEIVE_OPCODE_HEARTBEAT = 1,
    RECEIVE_OPCODE_INVALID_SESSION = 9,
    RECEIVE_OPCODE_HELLO = 10,
    RECEIVE_OPCODE_HEARTBEAT_ACK = 11,
} GatewayOpcodeReceive;

typedef enum {
    SEND_OPCODE_HEARTBEAT = 1,
    SEND_OPCODE_IDENTIFY = 2,
} GatewayOpcodeSend;

// https://discord.com/developers/docs/events/gateway-events#payload-structure
typedef struct {
    // Gateway opcode, which indicates the payload type
    int32_t op;
    // Event data
    cJSON *d_json;
    // Sequence number
    int32_t s;
    // Event name
    char *t;
} GatewayEventPayload;

// https://discord.com/developers/docs/events/gateway-events#identify-identify-structure
typedef struct {
    const char *token;
    struct {
        const char *os;
        const char *browser;
        const char *device;
    } properties;
    int32_t intents;
} IdentifyEventData;

typedef struct {
    int32_t heartbeat_interval;
} HelloEventData;

bool gateway_event_parse(uint8_t *data, size_t length,
                         GatewayEventPayload *out_payload);
void gateway_event_cleanup(GatewayEventPayload *payload);

// Receive Events

bool gateway_event_parse_hello(const cJSON *data, HelloEventData *out_data);

// Send Events

cJSON *gateway_event_create(int32_t op, int32_t seq, const char *type,
                            cJSON *data_json);
cJSON *gateway_event_identify(const IdentifyEventData *data);
cJSON *gateway_event_heartbeat(int32_t seq);

#endif // DISCORD_H
