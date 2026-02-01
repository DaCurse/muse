#include "transport.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

#define BUFFER_DEFAULT_CAPACITY (16384)

#define buffer_append(buffer, chunk, chunk_size)                               \
    do {                                                                       \
        if ((buffer)->length + chunk_size > (buffer)->capacity) {              \
            size_t new_capacity = (buffer)->capacity == 0                      \
                                      ? BUFFER_DEFAULT_CAPACITY                \
                                      : (buffer)->capacity;                    \
            while ((buffer)->length + chunk_size > new_capacity) {             \
                new_capacity *= 2;                                             \
            }                                                                  \
            uint8_t *ptr = realloc((buffer)->data, new_capacity);              \
            if (!ptr) {                                                        \
                fprintf(stderr, "error: out of memory");                       \
                exit(1);                                                       \
            }                                                                  \
            (buffer)->data = ptr;                                              \
            (buffer)->capacity = new_capacity;                                 \
        }                                                                      \
        memcpy(&((buffer)->data[(buffer)->length]), chunk, chunk_size);        \
        (buffer)->length += chunk_size;                                        \
    } while (0);

#define CURLOPT_CONNECT_ONLY_HEADERS (2L)

typedef struct {
    curl_socket_t sockfd;
} SocketContext;

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
    struct curl_slist *headers;
    HTTPCallback on_done;
    void *user_data;
    uint8_t *request_body;
} RequestContext;

static int timer_callback(CURLM *multi, long timeout_ms, void *userp) {
    (void)multi;

    MuseTransport *ts = (MuseTransport *)userp;
    ts->timeout_ms = timeout_ms;
    return 0;
}

int socket_callback(CURL *curl, curl_socket_t socket, int action, void *userp,
                    void *socketp) {
    (void)curl;

    MuseTransport *ts = (MuseTransport *)userp;
    SocketContext *ctx = (SocketContext *)socketp;
    struct epoll_event ev = {0};

    if (action == CURL_POLL_REMOVE) {
        if (ctx) {
            epoll_ctl(ts->epfd, EPOLL_CTL_DEL, socket, NULL);
            free(ctx);
        }
        return 0;
    }

    if (!ctx) {
        ctx = calloc(1, sizeof(*ctx));
        ctx->sockfd = socket;
        curl_multi_assign(ts->multi, socket, ctx);
    }

    ev.events = (action & CURL_POLL_IN ? EPOLLIN : 0) |
                (action & CURL_POLL_OUT ? EPOLLOUT : 0);
    ev.data.ptr = ctx;

    if (epoll_ctl(ts->epfd, EPOLL_CTL_ADD, socket, &ev) == -1 &&
        errno == EEXIST) {
        epoll_ctl(ts->epfd, EPOLL_CTL_MOD, socket, &ev);
    }

    return 0;
}

static void request_free(RequestContext *ctx) {
    if (ctx->headers)
        curl_slist_free_all(ctx->headers);
    free(ctx->data);
    free(ctx->request_body);
    free(ctx);
}

static void handle_multi_messages(MuseTransport *ts) {
    CURLMsg *msg;
    int pending;

    while ((msg = curl_multi_info_read(ts->multi, &pending))) {
        if (msg->msg == CURLMSG_DONE) {

            // Handle websocket
            if (msg->easy_handle == ts->ws_easy) {
                if (msg->data.result == CURLE_OK) {
                    ts->ws_handshake_done = true;
                } else {
                    fprintf(stderr, "WebSocket Disconnected/Error: %d\n",
                            msg->data.result);
                    transport_ws_close(ts);
                }
            }

            // Handle HTTP Request
            else {
                RequestContext *ctx = NULL;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &ctx);

                if (ctx) {
                    HTTPResponse res = {0};
                    res.result = msg->data.result;
                    res.data = ctx->data;
                    res.length = ctx->length;

                    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE,
                                      &res.status);

                    if (ctx->on_done) {
                        ctx->on_done(&res, ctx->user_data);
                    }

                    request_free(ctx);
                }

                curl_multi_remove_handle(ts->multi, msg->easy_handle);
                curl_easy_cleanup(msg->easy_handle);
            }
        }
    }
}

void transport_init(MuseTransport *ts, const char *user_agent, WSCallbacks cbs,
                    void *user_data) {
    ts->multi = curl_multi_init();
    ts->epfd = epoll_create1(0);
    ts->user_agent = user_agent;
    ts->ws_callbacks = cbs;
    ts->user_data = user_data;

    curl_multi_setopt(ts->multi, CURLMOPT_SOCKETFUNCTION, socket_callback);
    curl_multi_setopt(ts->multi, CURLMOPT_SOCKETDATA, ts);
    curl_multi_setopt(ts->multi, CURLMOPT_TIMERFUNCTION, timer_callback);
    curl_multi_setopt(ts->multi, CURLMOPT_TIMERDATA, ts);
}

bool transport_is_ws_open(MuseTransport *ts) { return ts->ws_easy != NULL; }

static void drain_ws_messages(MuseTransport *ts) {
    size_t rlen;
    const struct curl_ws_frame *meta;
    char chunk[4096];

    for (;;) {
        CURLcode res =
            curl_ws_recv(ts->ws_easy, chunk, sizeof(chunk), &rlen, &meta);

        if (res == CURLE_AGAIN) {
            break;
        }

        if (res != CURLE_OK) {
            transport_ws_close(ts);
            break;
        }

        bool is_data = (meta->flags & (CURLWS_TEXT | CURLWS_BINARY));
        bool is_cont = (meta->flags & CURLWS_CONT);

        if (is_data && rlen > 0) {
            buffer_append(&ts->current_message, chunk, rlen);
        } else {
            // TODO: Handle pings/pongs/close frames
        }

        // https://curl.se/libcurl/c/curl_ws_meta.html#CURLWSCONT
        if (is_data && !is_cont && meta->bytesleft == 0) {
            if (ts->ws_callbacks.on_message && ts->current_message.length > 0) {
                ts->ws_callbacks.on_message(ts, ts->current_message.data,
                                            ts->current_message.length);
            }
            ts->current_message.length = 0;
        }
    }
}

void transport_poll(MuseTransport *ts, int64_t default_timeout_ms) {
    int64_t wait_ms = ts->timeout_ms;
    if (wait_ms < 0)
        wait_ms = default_timeout_ms;
    if (wait_ms == 0)
        wait_ms = 1;

    struct epoll_event events[16];
    int num_fds = epoll_wait(
        ts->epfd, events, sizeof(events) / sizeof(events[0]), (int32_t)wait_ms);

    if (num_fds > 0) { // Convert epoll events to curl actions
        for (int i = 0; i < num_fds; i++) {
            SocketContext *ctx = (SocketContext *)events[i].data.ptr;
            int action = (events[i].events & EPOLLIN ? CURL_CSELECT_IN : 0) |
                         (events[i].events & EPOLLOUT ? CURL_CSELECT_OUT : 0);
            curl_multi_socket_action(ts->multi, ctx->sockfd, action,
                                     &ts->running_handles);
        }
    } else { // epoll timeout handling
        curl_multi_socket_action(ts->multi, CURL_SOCKET_TIMEOUT, 0,
                                 &ts->running_handles);
    }

    // Handle multi messages
    handle_multi_messages(ts);
    curl_multi_socket_action(ts->multi, CURL_SOCKET_TIMEOUT, 0,
                             &ts->running_handles);

    // Fire websocket on connect once
    if (ts->ws_handshake_done && !ts->ws_on_connect_fired) {
        ts->ws_on_connect_fired = true;
        if (ts->ws_callbacks.on_connect) {
            ts->ws_callbacks.on_connect(ts);
        }
    }

    // Drain websocket messages
    if (ts->ws_easy && ts->ws_handshake_done) {
        drain_ws_messages(ts);
    }
}

void transport_ws_open(MuseTransport *ts, const char *url) {
    if (ts->ws_easy) {
        transport_ws_close(ts);
    }

    CURL *ws_easy = curl_easy_init();
    curl_easy_setopt(ws_easy, CURLOPT_URL, url);
    curl_easy_setopt(ws_easy, CURLOPT_USERAGENT, ts->user_agent);
    curl_easy_setopt(ws_easy, CURLOPT_CONNECT_ONLY,
                     CURLOPT_CONNECT_ONLY_HEADERS);
    // Set to NULL to not confuse with request handles
    curl_easy_setopt(ws_easy, CURLOPT_PRIVATE, NULL);

    curl_multi_add_handle(ts->multi, ws_easy);
    ts->ws_easy = ws_easy;

    // Kickstart the connection process
    curl_multi_socket_action(ts->multi, CURL_SOCKET_TIMEOUT, 0,
                             &ts->running_handles);
}

void transport_ws_close(MuseTransport *t) {
    if (t->ws_easy) {
        curl_multi_remove_handle(t->multi, t->ws_easy);
        curl_easy_cleanup(t->ws_easy);
        t->ws_easy = NULL;
    }
    t->ws_handshake_done = false;
    t->ws_on_connect_fired = false;
    t->current_message.length = 0;

    if (t->ws_callbacks.on_disconnect)
        t->ws_callbacks.on_disconnect(t);
}

CURLcode transport_ws_send(MuseTransport *ts, const uint8_t *data,
                           size_t length) {
    if (!ts->ws_handshake_done)
        return CURLE_COULDNT_CONNECT;

    size_t sent;
    return curl_ws_send(ts->ws_easy, data, length, &sent, 0, CURLWS_TEXT);
}

CURLcode transport_ws_send_json(MuseTransport *ts, const cJSON *data) {
    char *json_str = cJSON_PrintUnformatted(data);
    if (!json_str) {
        return CURLE_FAILED_INIT;
    }

    CURLcode res =
        transport_ws_send(ts, (const uint8_t *)json_str, strlen(json_str));
    free(json_str);

    return res;
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

void transport_url_encode(const char *input, char *output, size_t output_size) {
    CURL *curl = curl_easy_init();
    if (curl) {
        char *encoded = curl_easy_escape(curl, input, 0);
        snprintf(output, output_size, "%s", encoded);
        curl_free(encoded);
        curl_easy_cleanup(curl);
    }
}

void transport_http_get(MuseTransport *ts, const char *url,
                        HTTPCallback on_done, void *user_data) {
    CURL *easy = curl_easy_init();
    RequestContext *ctx = calloc(1, sizeof(RequestContext));
    ctx->on_done = on_done;
    ctx->user_data = user_data;

#ifdef TRANSPORT_HTTP_GET_DEBUG
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
#endif
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, ts->user_agent);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);

    curl_multi_add_handle(ts->multi, easy);
}

void transport_http_post(MuseTransport *ts, const char *url,
                         const uint8_t *body, size_t content_length,
                         const char *content_type,
                         const struct curl_slist *extra_headers,
                         HTTPCallback on_done, void *user_data) {
    CURL *easy = curl_easy_init();
    RequestContext *ctx = calloc(1, sizeof(RequestContext));
    ctx->on_done = on_done;
    ctx->user_data = user_data;

    ctx->request_body = malloc(content_length);
    memcpy(ctx->request_body, body, content_length);

    char header[256];
    snprintf(header, sizeof(header), "Content-Type: %s", content_type);
    ctx->headers = curl_slist_append(NULL, header);

    const struct curl_slist *hdr = extra_headers;
    while (hdr) {
        ctx->headers = curl_slist_append(ctx->headers, hdr->data);
        hdr = hdr->next;
    }

#ifdef TRANSPORT_HTTP_POST_DEBUG
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
#endif
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, ts->user_agent);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, ctx->request_body);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)content_length);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);

    curl_multi_add_handle(ts->multi, easy);
}

void transport_destroy(MuseTransport *ts) {
    if (!ts)
        return;

    // Clean up all remaining requests
    CURL **handles = curl_multi_get_handles(ts->multi);
    if (handles) {
        for (int i = 0; handles[i]; i++) {
            if (handles[i] == ts->ws_easy)
                continue;

            void *ptr;
            curl_easy_getinfo(handles[i], CURLINFO_PRIVATE, &ptr);

            if (ptr) {
                request_free((RequestContext *)ptr);
            }

            curl_multi_remove_handle(ts->multi, handles[i]);
            curl_easy_cleanup(handles[i]);
        }
        curl_free(handles);
    }

    if (ts->ws_easy) {
        curl_multi_remove_handle(ts->multi, ts->ws_easy);
        curl_easy_cleanup(ts->ws_easy);
        ts->ws_easy = NULL;
    }

    curl_multi_cleanup(ts->multi);

    if (ts->epfd) {
#ifndef _WIN32
        close(ts->epfd);
#else
        epoll_close(ts->epfd);
#endif
    }

    free(ts->current_message.data);
}
