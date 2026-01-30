#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bot.h"

void test_on_connect(MuseBot *bot) {
    printf("WebSocket connected!\n");
    char *message = "Hello, WebSocket!";
    CURLcode res = bot_ws_send(bot, (uint8_t *)message, strlen(message));
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to send initial message: %d\n", res);
    }
}

void test_on_message(MuseBot *bot, const uint8_t *data, size_t length) {
    printf("Received message: %.*s\n", (int)length, data);
    char *message = "Hello, WebSocket!";
    CURLcode res = bot_ws_send(bot, (uint8_t *)message, strlen(message));
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to send message: %d\n", res);
    }
}

void test_on_response(MuseResponse *res, void *user_data) {
    if (res->result == CURLE_OK) {
        printf("HTTP GET Response (%ld): %.*s\n", res->status, (int)res->length,
               res->data);
    } else {
        fprintf(stderr, "HTTP GET failed: %d\n", res->result);
    }
}

void test_on_post_response(MuseResponse *res, void *user_data) {
    if (res->result == CURLE_OK) {
        printf("HTTP POST Response (%ld): %.*s\n", res->status,
               (int)res->length, res->data);
    } else {
        fprintf(stderr, "HTTP POST failed: %d\n", res->result);
    }
}

int main() {
    MuseBot bot = {0};
    bot_init(&bot);

    bot_ws_open(&bot, "wss://ws.postman-echo.com/raw",
                (MuseWSCallbacks){
                    .on_connect = test_on_connect,
                    .on_message = test_on_message,
                });

    bot_http_get(&bot, "https://postman-echo.com/get", test_on_response, NULL);
    char *body = "field1=value1&field2=value2";
    bot_http_post(&bot, "https://postman-echo.com/post", (uint8_t *)body,
                  strlen(body), "application/x-www-form-urlencoded",
                  test_on_post_response, NULL);

    while (bot_still_running(&bot)) {
        bot_poll(&bot, 1000L);
    }

    bot_destroy(&bot);
    return 0;
}

