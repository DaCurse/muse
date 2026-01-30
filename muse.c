#include <stdio.h>

#include "bot.h"

void test_on_connect(MuseBot *bot) {
    printf("WebSocket connected!\n");
    CURLcode res = bot_ws_send(bot, "Hello, WebSocket!");
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to send initial message: %d\n", res);
    }
}

void test_on_message(MuseBot *bot, const char *text, size_t len) {
    printf("Received message: %.*s\n", (int)len, text);
    bot_ws_send(bot, "Hello, WebSocket!");
}

void test_on_response(MuseResponse *res, void *user_data) {
    if (res->result == CURLE_OK) {
        printf("HTTP GET Response (%d): %.*s\n", res->status, (int)res->len,
               res->data);
    } else {
        fprintf(stderr, "HTTP GET failed: %d\n", res->result);
    }
}

void test_on_post_response(MuseResponse *res, void *user_data) {
    if (res->result == CURLE_OK) {
        printf("HTTP POST Response (%d): %.*s\n", res->status, (int)res->len,
               res->data);
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

    // bot_http_get(&bot, "https://postman-echo.com/get", test_on_response, NULL);
    // bot_http_post(
    //     &bot, "https://postman-echo.com/post", "field1=value1&field2=value2",
    //     "application/x-www-form-urlencoded", test_on_post_response, NULL);

    while (bot_still_running(&bot)) {
        bot_poll(&bot, 1000L);
    }

    bot_destroy(&bot);
    return 0;
}

