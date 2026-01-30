#include "bot.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

#define UNUSED(x) (void)(x)
#define DEFAULT_BUFFER_CAPACITY (16384)

#define buffer_append(buffer, chunk, chunk_size)                               \
    do {                                                                       \
        if ((buffer)->length + chunk_size + 1 > (buffer)->capacity) {          \
            size_t new_capacity = (buffer)->capacity == 0                      \
                                      ? DEFAULT_BUFFER_CAPACITY                \
                                      : (buffer)->capacity;                    \
            while ((buffer)->length + chunk_size + 1 > new_capacity) {         \
                new_capacity *= 2;                                             \
            }                                                                  \
            char *ptr = realloc((buffer)->data, new_capacity);                 \
            if (!ptr) {                                                        \
                fprintf(stderr, "error: out of memory");                       \
                exit(1);                                                       \
            }                                                                  \
            (buffer)->data = ptr;                                              \
            (buffer)->capacity = new_capacity;                                 \
        }                                                                      \
        memcpy(&((buffer)->data[(buffer)->length]), chunk, chunk_size);        \
        (buffer)->length += chunk_size;                                        \
        (buffer)->data[(buffer)->length] = '\0';                               \
    } while (0);

#define CURLOPT_CONNECT_ONLY_HEADERS (2L)

typedef struct {
    curl_socket_t sockfd;
} SocketContext;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    struct curl_slist *headers;
    MuseHTTPCallback on_done;
    void *user_data;
} RequestContext;

static int timer_callback(CURLM *multi, long timeout_ms, void *userp) {
    MuseBot *bot = (MuseBot *)userp;
    bot->timeout_ms = timeout_ms;
    return 0;
}

int socket_callback(CURL *curl, curl_socket_t socket, int action, void *userp,
                    void *socketp) {
    UNUSED(curl);

    MuseBot *bot = (MuseBot *)userp;
    SocketContext *ctx = (SocketContext *)socketp;
    struct epoll_event ev = {0};

    if (action == CURL_POLL_REMOVE) {
        if (ctx) {
            epoll_ctl(bot->epfd, EPOLL_CTL_DEL, socket, NULL);
            free(ctx);
        }
        return 0;
    }

    if (!ctx) {
        ctx = calloc(1, sizeof(*ctx));
        ctx->sockfd = socket;
        curl_multi_assign(bot->multi, socket, ctx);
    }

    ev.events = (action & CURL_POLL_IN ? EPOLLIN : 0) |
                (action & CURL_POLL_OUT ? EPOLLOUT : 0);
    ev.data.ptr = ctx;

    if (epoll_ctl(bot->epfd, EPOLL_CTL_ADD, socket, &ev) == -1 &&
        errno == EEXIST) {
        epoll_ctl(bot->epfd, EPOLL_CTL_MOD, socket, &ev);
    }

    return 0;
}

static void request_cleanup(RequestContext *ctx) {
    if (ctx->headers)
        curl_slist_free_all(ctx->headers);
    free(ctx->data);
    free(ctx);
}

static void handle_multi_messages(MuseBot *bot) {
    CURLMsg *msg;
    int pending;

    while ((msg = curl_multi_info_read(bot->multi, &pending))) {
        if (msg->msg == CURLMSG_DONE) {

            // Handle websocket
            if (msg->easy_handle == bot->ws_easy) {
                if (msg->data.result == CURLE_OK) {
                    bot->ws_handshake_done = true;
                } else {
                    fprintf(stderr, "WebSocket Disconnected/Error: %d\n",
                            msg->data.result);
                    // TODO: Handle reconnection?
                }
            }

            // Handle HTTP Request
            else {
                RequestContext *ctx = NULL;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &ctx);

                if (ctx) {
                    MuseResponse res = {0};
                    res.result = msg->data.result;
                    res.data = ctx->data ? ctx->data : "";
                    res.len = ctx->length;

                    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE,
                                      &res.status);

                    if (ctx->on_done) {
                        ctx->on_done(&res, ctx->user_data);
                    }

                    request_cleanup(ctx);
                }

                curl_multi_remove_handle(bot->multi, msg->easy_handle);
                curl_easy_cleanup(msg->easy_handle);
            }
        }
    }
}

void bot_init(MuseBot *bot) {
    bot->multi = curl_multi_init();
    bot->epfd = epoll_create1(0);

    curl_multi_setopt(bot->multi, CURLMOPT_SOCKETFUNCTION, socket_callback);
    curl_multi_setopt(bot->multi, CURLMOPT_SOCKETDATA, bot);
    curl_multi_setopt(bot->multi, CURLMOPT_TIMERFUNCTION, timer_callback);
    curl_multi_setopt(bot->multi, CURLMOPT_TIMERDATA, bot);
}

bool bot_still_running(MuseBot *bot) {
    if (bot->ws_easy != NULL)
        return true;
    if (bot->running_handles > 0)
        return true;
    return false;
}

static void drain_ws_messages(MuseBot *bot) {
    size_t rlen;
    const struct curl_ws_frame *meta;
    char chunk[4096];

    for (;;) {
        CURLcode res =
            curl_ws_recv(bot->ws_easy, chunk, sizeof(chunk), &rlen, &meta);

        if (res == CURLE_AGAIN) {
            break;
        }

        if (res != CURLE_OK) {
            bot->ws_handshake_done = false;
            bot->ws_on_connect_fired = false;
            bot->current_message.length = 0;
            break;
        }

        if (rlen > 0) {
            buffer_append(&bot->current_message, chunk, rlen);
        }

        if (meta->bytesleft == 0) {
            if (bot->ws_callbacks.on_message &&
                bot->current_message.length > 0) {
                bot->ws_callbacks.on_message(bot, bot->current_message.data,
                                             bot->current_message.length);
            }
            bot->current_message.length = 0;
        }
    }
}

void bot_poll(MuseBot *bot, int64_t default_timeout_ms) {
    int64_t wait_ms = bot->timeout_ms;
    if (wait_ms < 0)
        wait_ms = default_timeout_ms;
    if (wait_ms == 0)
        wait_ms = 1;

    struct epoll_event events[16];
    int num_fds =
        epoll_wait(bot->epfd, events, sizeof(events) / sizeof(events[0]),
                   (int32_t)wait_ms);

    // Convert epoll events to curl actions
    if (num_fds > 0) {
        for (int i = 0; i < num_fds; i++) {
            SocketContext *ctx = (SocketContext *)events[i].data.ptr;
            int action = (events[i].events & EPOLLIN ? CURL_CSELECT_IN : 0) |
                         (events[i].events & EPOLLOUT ? CURL_CSELECT_OUT : 0);
            curl_multi_socket_action(bot->multi, ctx->sockfd, action,
                                     &bot->running_handles);
        }
        // epoll timeout handling
    } else {
        curl_multi_socket_action(bot->multi, CURL_SOCKET_TIMEOUT, 0,
                                 &bot->running_handles);
    }

    // Handle multi messages
    handle_multi_messages(bot);
    curl_multi_socket_action(bot->multi, CURL_SOCKET_TIMEOUT, 0,
                             &bot->running_handles);

    // Fire websocket on connect once
    if (bot->ws_handshake_done && !bot->ws_on_connect_fired) {
        bot->ws_on_connect_fired = true;
        if (bot->ws_callbacks.on_connect) {
            bot->ws_callbacks.on_connect(bot);
        }
    }

    // Drain websocket messages
    if (bot->ws_handshake_done) {
        drain_ws_messages(bot);
    }
}

void bot_ws_open(MuseBot *bot, const char *url, MuseWSCallbacks cbs) {
    bot->ws_callbacks.on_connect = cbs.on_connect;
    bot->ws_callbacks.on_message = cbs.on_message;

    CURL *ws_easy = curl_easy_init();
    curl_easy_setopt(ws_easy, CURLOPT_URL, url);
    curl_easy_setopt(ws_easy, CURLOPT_CONNECT_ONLY,
                     CURLOPT_CONNECT_ONLY_HEADERS);
    // Set to NULL to not confuse with request handles
    curl_easy_setopt(ws_easy, CURLOPT_PRIVATE, NULL);

    curl_multi_add_handle(bot->multi, ws_easy);
    bot->ws_easy = ws_easy;

    // Kickstart the connection process
    curl_multi_socket_action(bot->multi, CURL_SOCKET_TIMEOUT, 0,
                             &bot->running_handles);
}

CURLcode bot_ws_send(MuseBot *bot, const char *text) {
    if (!bot->ws_handshake_done)
        return CURLE_COULDNT_CONNECT;

    size_t sent;
    return curl_ws_send(bot->ws_easy, text, strlen(text), &sent, 0,
                        CURLWS_TEXT);
}

static size_t http_write_callback(void *data, size_t size, size_t nmemb,
                                  void *userp) {
    size_t realsize = size * nmemb;
    RequestContext *ctx = (RequestContext *)userp;

    buffer_append(ctx, data, realsize);
    if (!ctx->data) {
        return 0;
    }

    return realsize;
}

void bot_http_get(MuseBot *bot, const char *url, MuseHTTPCallback on_done,
                  void *user_data) {
    CURL *easy = curl_easy_init();
    RequestContext *ctx = calloc(1, sizeof(RequestContext));
    ctx->on_done = on_done;
    ctx->user_data = user_data;

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);

    curl_multi_add_handle(bot->multi, easy);
}

void bot_http_post(MuseBot *bot, const char *url, const char *body,
                   const char *content_type, MuseHTTPCallback on_done,
                   void *user_data) {
    CURL *easy = curl_easy_init();
    RequestContext *ctx = calloc(1, sizeof(RequestContext));
    ctx->on_done = on_done;
    ctx->user_data = user_data;

    char header[256];
    snprintf(header, sizeof(header), "Content-Type: %s", content_type);
    ctx->headers = curl_slist_append(NULL, header);

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);

    curl_multi_add_handle(bot->multi, easy);
}

void bot_destroy(MuseBot *bot) {
    if (!bot)
        return;

    // Clean up all remaining requests
    int num_handles;
    CURL **handles = curl_multi_get_handles(bot->multi);
    if (handles) {
        for (int i = 0; handles[i]; i++) {
            if (handles[i] == bot->ws_easy)
                continue;

            void *ptr;
            curl_easy_getinfo(handles[i], CURLINFO_PRIVATE, &ptr);

            if (ptr) {
                request_cleanup((RequestContext *)ptr);
            }
        }
        curl_free(handles);
    }

    curl_multi_cleanup(bot->multi);

    if (bot->epfd) {
#ifndef _WIN32
        close(bot->epfd);
#else
        epoll_close(bot->epfd);
#endif
    }

    free(bot->current_message.data);
}
