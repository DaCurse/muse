#ifndef BOT_H
#define BOT_H

#include <curl/curl.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#else
#define _WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct MuseBot MuseBot;

typedef struct {
    void (*on_connect)(struct MuseBot *bot);
    void (*on_message)(struct MuseBot *bot, const uint8_t *data, size_t length);
} MuseWSCallbacks;

typedef struct {
    uint8_t *data;
    size_t length;
    // status code is a long in curl
    int64_t status;
    CURLcode result;
} MuseResponse;

// NOTE: The data in the response is only valid within the callback
typedef void (*MuseHTTPCallback)(MuseResponse *res, void *user_data);

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} WSMessage;

typedef struct MuseBot {
    CURLM *multi;
#ifndef _WIN32
    int epfd;
#else
    HANDLE epfd;
#endif
    int64_t timeout_ms;
    CURL *ws_easy;
    int running_handles;

    bool ws_handshake_done;
    bool ws_on_connect_fired;

    MuseWSCallbacks ws_callbacks;
    WSMessage current_message;
} MuseBot;

void bot_init(MuseBot *bot);
bool bot_still_running(MuseBot *bot);
void bot_poll(MuseBot *bot, int64_t default_timeout_ms);
void bot_ws_open(MuseBot *bot, const char *url, MuseWSCallbacks cbs);
CURLcode bot_ws_send(MuseBot *bot, const uint8_t *data, size_t length);
void bot_http_get(MuseBot *bot, const char *url, MuseHTTPCallback on_done,
                  void *user_data);
void bot_http_post(MuseBot *bot, const char *url, const uint8_t *body,
                   size_t content_length, const char *content_type,
                   MuseHTTPCallback on_done, void *user_data);
void bot_destroy(MuseBot *bot);

#endif // BOT_H
