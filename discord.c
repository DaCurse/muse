#include "discord.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool gateway_event_parse(uint8_t *data, size_t length,
                         GatewayEventPayload *out_payload) {
    bool success = true;

    cJSON *payload_json = cJSON_ParseWithLength((const char *)data, length);
    if (!payload_json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "Failed to parse JSON payload: %s\n", error_ptr);
        }
        success = false;
        goto cleanup;
    }

    const cJSON *op = cJSON_GetObjectItemCaseSensitive(payload_json, "op");
    const cJSON *event_data =
        cJSON_GetObjectItemCaseSensitive(payload_json, "d");
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(payload_json, "s");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(payload_json, "t");

    if (cJSON_IsNumber(op)) {
        out_payload->op = (int)op->valueint;
    } else {
        fprintf(stderr, "Invalid/missing 'op' field in payload\n");
        success = false;
        goto cleanup;
    }

    out_payload->d_json = event_data ? cJSON_Duplicate(event_data, true) : NULL;
    out_payload->s = cJSON_IsNumber(seq) ? (int)seq->valueint : -1;
    out_payload->t = cJSON_IsString(type) && (type->valuestring != NULL)
                         ? strdup(type->valuestring)
                         : NULL;

cleanup:
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
    cJSON *payload_json = cJSON_CreateObject();
    if (!payload_json) {
        fprintf(stderr, "Failed to create Gateway Event payload JSON\n");
        return NULL;
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
}

cJSON *gateway_event_identify(const IdentifyEventData *data) {
    cJSON *d_json = cJSON_CreateObject();
    if (!d_json) {
        fprintf(stderr, "Failed to create 'd' object for Identify payload\n");
        return NULL;
    }

    cJSON_AddStringToObject(d_json, "token", data->token);

    cJSON *properties_json = cJSON_CreateObject();
    if (!properties_json) {
        fprintf(stderr,
                "Failed to create 'properties' object for Identify payload\n");
        cJSON_Delete(d_json);
        return NULL;
    }

    cJSON_AddStringToObject(properties_json, "os", data->properties.os);
    cJSON_AddStringToObject(properties_json, "browser",
                            data->properties.browser);
    cJSON_AddStringToObject(properties_json, "device", data->properties.device);

    cJSON_AddItemToObject(d_json, "properties", properties_json);
    cJSON_AddNumberToObject(d_json, "intents", data->intents);

    cJSON *payload_json =
        gateway_event_create(SEND_OPCODE_IDENTIFY, -1, NULL, d_json);
    if (!payload_json) {
        cJSON_Delete(d_json);
        return NULL;
    }

    return payload_json;
}

cJSON *gateway_event_heartbeat(int32_t seq) {
    return gateway_event_create(SEND_OPCODE_HEARTBEAT, -1, NULL,
                                seq == -1 ? NULL : cJSON_CreateNumber(seq));
}

void gateway_event_cleanup(GatewayEventPayload *payload) {
    if (payload->d_json) {
        cJSON_Delete(payload->d_json);
    }
    if (payload->t) {
        free(payload->t);
    }
}
