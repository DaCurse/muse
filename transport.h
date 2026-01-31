#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h> // for close(2)
#else
#define _WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct MuseTransport MuseTransport;

typedef struct {
    void (*on_connect)(struct MuseTransport *ts);
    void (*on_disconnect)(struct MuseTransport *ts);
    void (*on_message)(struct MuseTransport *ts, const uint8_t *data,
                       size_t length);
} WSCallbacks;

typedef struct {
    uint8_t *data;
    size_t length;
    // status code is a long in curl
    int64_t status;
    CURLcode result;
} HTTPResponse;

// NOTE: The data in the response is only valid within the callback
typedef void (*HTTPCallback)(HTTPResponse *res);

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} WSMessage;

typedef struct MuseTransport {
    CURLM *multi;
#ifndef _WIN32
    int epfd;
#else
    HANDLE epfd;
#endif
    int64_t timeout_ms;
    const char *user_agent;
    CURL *ws_easy;
    int running_handles;

    bool ws_handshake_done;
    bool ws_on_connect_fired;

    void *user_data;

    WSCallbacks ws_callbacks;
    WSMessage current_message;
} MuseTransport;

void transport_init(MuseTransport *ts, const char *user_agent, WSCallbacks cbs,
                    void *user_data);
bool transport_is_ws_open(MuseTransport *ts);
void transport_poll(MuseTransport *ts, int64_t default_timeout_ms);
void transport_ws_open(MuseTransport *ts, const char *url);
void transport_ws_close(MuseTransport *t);
CURLcode transport_ws_send(MuseTransport *ts, const uint8_t *data,
                           size_t length);
CURLcode transport_ws_send_json(MuseTransport *ts, const cJSON *data);
void transport_http_get(MuseTransport *ts, const char *url,
                        HTTPCallback on_done);
void transport_http_post(MuseTransport *ts, const char *url,
                         const uint8_t *body, size_t content_length,
                         const char *content_type, HTTPCallback on_done);
void transport_destroy(MuseTransport *ts);

#endif // TRANSPORT_H
