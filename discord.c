#include "discord.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool gateway_event_parse(uint8_t *data, size_t length,
                         GatewayEventPayload *out_payload) {
    bool success = false;
    cJSON *payload_json = NULL;
    const cJSON *op = NULL;
    const cJSON *event_data = NULL;
    const cJSON *seq = NULL;
    const cJSON *type = NULL;

    payload_json = cJSON_ParseWithLength((const char *)data, length);
    if (!payload_json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "Failed to parse JSON payload: %s\n", error_ptr);
        }
        goto cleanup;
    }

    op = cJSON_GetObjectItemCaseSensitive(payload_json, "op");
    event_data = cJSON_GetObjectItemCaseSensitive(payload_json, "d");
    seq = cJSON_GetObjectItemCaseSensitive(payload_json, "s");
    type = cJSON_GetObjectItemCaseSensitive(payload_json, "t");

    if (!cJSON_IsNumber(op)) {
        fprintf(stderr, "Invalid/missing 'op' field in payload\n");
        goto cleanup;
    }

    out_payload->op = (int)op->valueint;
    out_payload->d_json = event_data ? cJSON_Duplicate(event_data, true) : NULL;
    out_payload->s = cJSON_IsNumber(seq) ? (int)seq->valueint : -1;
    out_payload->t = cJSON_IsString(type) && (type->valuestring != NULL)
                         ? strdup(type->valuestring)
                         : NULL;
    success = true;

cleanup:
    if (payload_json)
        cJSON_Delete(payload_json);
    return success;
}

bool gateway_event_parse_hello(const cJSON *data, HelloEventData *out_data) {
    const cJSON *heartbeat_interval =
        cJSON_GetObjectItemCaseSensitive(data, "heartbeat_interval");
    if (!cJSON_IsNumber(heartbeat_interval)) {
        fprintf(stderr,
                "Invalid/missing 'heartbeat_interval' in Hello event data\n");
        return false;
    }

    out_data->heartbeat_interval = (int32_t)heartbeat_interval->valueint;
    return true;
}

cJSON *gateway_event_create(int32_t op, int32_t seq, const char *type,
                            cJSON *data_json) {
    cJSON *payload_json = NULL;

    payload_json = cJSON_CreateObject();
    if (!payload_json) {
        fprintf(stderr, "Failed to create Gateway Event payload JSON\n");
        goto cleanup;
    }

    cJSON_AddNumberToObject(payload_json, "op", op);
    if (data_json) {
        cJSON_AddItemToObject(payload_json, "d", data_json);
    } else {
        cJSON_AddNullToObject(payload_json, "d");
    }
    if (seq >= 0) {
        cJSON_AddNumberToObject(payload_json, "s", seq);
    }
    if (type) {
        cJSON_AddStringToObject(payload_json, "t", type);
    }

    return payload_json;

cleanup:
    if (payload_json)
        cJSON_Delete(payload_json);
    return NULL;
}

cJSON *gateway_event_identify(const IdentifyEventData *data) {
    cJSON *d_json = NULL;
    cJSON *properties_json = NULL;
    cJSON *payload_json = NULL;

    d_json = cJSON_CreateObject();
    if (!d_json) {
        fprintf(stderr, "Failed to create 'd' object for Identify payload\n");
        goto cleanup;
    }

    cJSON_AddStringToObject(d_json, "token", data->token);

    properties_json = cJSON_CreateObject();
    if (!properties_json) {
        fprintf(stderr,
                "Failed to create 'properties' object for Identify payload\n");
        goto cleanup;
    }

    cJSON_AddStringToObject(properties_json, "os", data->properties.os);
    cJSON_AddStringToObject(properties_json, "browser",
                            data->properties.browser);
    cJSON_AddStringToObject(properties_json, "device", data->properties.device);

    cJSON_AddItemToObject(d_json, "properties", properties_json);
    cJSON_AddNumberToObject(d_json, "intents", data->intents);

    payload_json = gateway_event_create(SEND_OPCODE_IDENTIFY, -1, NULL, d_json);
    if (!payload_json) {
        goto cleanup;
    }

    return payload_json;

cleanup:
    if (properties_json)
        cJSON_Delete(properties_json);
    if (d_json)
        cJSON_Delete(d_json);
    return NULL;
}

cJSON *gateway_event_heartbeat(int32_t seq) {
    return gateway_event_create(SEND_OPCODE_HEARTBEAT, -1, NULL,
                                seq == -1 ? NULL : cJSON_CreateNumber(seq));
}

static cJSON *serialize_embed(const DiscordEmbed *embed) {
    cJSON *embed_json = NULL;
    cJSON *fields_json = NULL;
    cJSON *field_json = NULL;
    cJSON *image_json = NULL;
    cJSON *thumbnail_json = NULL;

    embed_json = cJSON_CreateObject();
    if (!embed_json) {
        fprintf(stderr, "Failed to create embed JSON object\n");
        goto cleanup;
    }

    if (embed->title) {
        cJSON_AddStringToObject(embed_json, "title", embed->title);
    }
    if (embed->type) {
        cJSON_AddStringToObject(embed_json, "type", embed->type);
    }
    if (embed->description) {
        cJSON_AddStringToObject(embed_json, "description", embed->description);
    }
    if (embed->color != -1) {
        cJSON_AddNumberToObject(embed_json, "color", embed->color);
    }

    if (embed->image.url) {
        image_json = cJSON_CreateObject();
        if (!image_json) {
            fprintf(stderr, "Failed to create embed image JSON object\n");
            goto cleanup;
        }
        cJSON_AddStringToObject(image_json, "url", embed->image.url);
        cJSON_AddItemToObject(embed_json, "image", image_json);
        image_json = NULL;
    }

    if (embed->thumbnail.url) {
        thumbnail_json = cJSON_CreateObject();
        if (!thumbnail_json) {
            fprintf(stderr, "Failed to create embed thumbnail JSON object\n");
            goto cleanup;
        }
        cJSON_AddStringToObject(thumbnail_json, "url", embed->thumbnail.url);
        cJSON_AddItemToObject(embed_json, "thumbnail", thumbnail_json);
        thumbnail_json = NULL;
    }

    fields_json = cJSON_CreateArray();
    if (!fields_json) {
        fprintf(stderr, "Failed to create embed fields JSON array\n");
        goto cleanup;
    }

    for (int i = 0; i < MAX_EMBED_FIELDS; i++) {
        const DiscordEmbedField *field = &embed->fields[i];
        if (!field->name || !field->value) {
            continue;
        }

        field_json = cJSON_CreateObject();
        if (!field_json) {
            fprintf(stderr, "Failed to create embed field JSON object\n");
            goto cleanup;
        }

        cJSON_AddStringToObject(field_json, "name", field->name);
        cJSON_AddStringToObject(field_json, "value", field->value);
        cJSON_AddBoolToObject(field_json, "inline", field->inline_field);

        cJSON_AddItemToArray(fields_json, field_json);
        field_json = NULL;
    }

    cJSON_AddItemToObject(embed_json, "fields", fields_json);
    return embed_json;

cleanup:
    if (field_json)
        cJSON_Delete(field_json);
    if (fields_json)
        cJSON_Delete(fields_json);
    if (image_json)
        cJSON_Delete(image_json);
    if (thumbnail_json)
        cJSON_Delete(thumbnail_json);
    if (embed_json)
        cJSON_Delete(embed_json);
    return NULL;
}

cJSON *rest_create_message(const DiscordCreateMessage *message) {
    cJSON *message_json = NULL;
    cJSON *embeds_json = NULL;
    cJSON *embed_json = NULL;

    message_json = cJSON_CreateObject();
    if (!message_json) {
        fprintf(stderr, "Failed to create message JSON object\n");
        goto cleanup;
    }

    cJSON_AddStringToObject(message_json, "content", message->content);
    cJSON_AddNumberToObject(message_json, "nonce", message->nonce);

    embeds_json = cJSON_CreateArray();
    if (!embeds_json) {
        fprintf(stderr, "Failed to create embeds JSON array\n");
        goto cleanup;
    }
    for (int i = 0; i < MAX_MESSAGE_EMBEDS; i++) {
        const DiscordEmbed *embed = &message->embeds[i];
        if (!embed->title && !embed->description) {
            continue;
        }

        embed_json = serialize_embed(embed);
        if (!embed_json) {
            goto cleanup;
        }

        cJSON_AddItemToArray(embeds_json, embed_json);
        embed_json = NULL;
    }

    cJSON_AddItemToObject(message_json, "embeds", embeds_json);
    return message_json;

cleanup:
    if (embed_json)
        cJSON_Delete(embed_json);
    if (embeds_json)
        cJSON_Delete(embeds_json);
    if (message_json)
        cJSON_Delete(message_json);
    return NULL;
}

void gateway_event_cleanup(GatewayEventPayload *payload) {
    if (payload->d_json) {
        cJSON_Delete(payload->d_json);
    }
    if (payload->t) {
        free(payload->t);
    }
}
