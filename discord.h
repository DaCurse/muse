#ifndef DISCORD_H
#define DISCORD_H

#include <cjson/cJSON.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_EMBED_FIELDS (25)
#define MAX_MESSAGE_EMBEDS (10)

#define FOREACH_EMBED_TYPE(TYPE)                                               \
    TYPE(EMBED_TYPE_RICH)                                                      \
    TYPE(EMBED_TYPE_IMAGE)                                                     \
    TYPE(EMBED_TYPE_VIDEO)                                                     \
    TYPE(EMBED_TYPE_GIFV)                                                      \
    TYPE(EMBED_TYPE_ARTICLE)                                                   \
    TYPE(EMBED_TYPE_LINK)

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

typedef enum { FOREACH_EMBED_TYPE(GENERATE_ENUM) } DiscordEmbedType;

static const char *DiscordEmbedTypeStrings[] = {
    FOREACH_EMBED_TYPE(GENERATE_STRING)};

// https://discord.com/developers/docs/topics/opcodes-and-status-codes#gateway-gateway-opcodes
typedef enum {
    RECEIVE_OPCODE_DISPATCH = 0,
    RECEIVE_OPCODE_HEARTBEAT = 1,
    RECEIVE_OPCODE_RECONNECT = 7,
    RECEIVE_OPCODE_INVALID_SESSION = 9,
    RECEIVE_OPCODE_HELLO = 10,
    RECEIVE_OPCODE_HEARTBEAT_ACK = 11,
} GatewayOpcodeReceive;

typedef enum {
    SEND_OPCODE_HEARTBEAT = 1,
    SEND_OPCODE_IDENTIFY = 2,
    SEND_OPCODE_RESUME = 6,
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

// https://discord.com/developers/docs/resources/message#embed-object-embed-image-structure
typedef struct {
    const char *url;
} DiscordEmbedImage;

// https://discord.com/developers/docs/resources/message#embed-object-embed-field-structure
typedef struct {
    const char *name;
    const char *value;
    bool inline_field;
} DiscordEmbedField;

// https://discord.com/developers/docs/resources/message#embed-object
typedef struct {
    const char *title;
    const char *type;
    const char *description;
    int32_t color;
    DiscordEmbedImage image;
    DiscordEmbedImage thumbnail;
    DiscordEmbedField fields[MAX_EMBED_FIELDS];
} DiscordEmbed;

// https://discord.com/developers/docs/resources/message#create-message
typedef struct {
    const char *content;
    int32_t nonce;
    const DiscordEmbed embeds[MAX_MESSAGE_EMBEDS];
} DiscordCreateMessage;

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

cJSON *rest_create_message(const DiscordCreateMessage *message);

#endif // DISCORD_H
