
#include "links.h"

#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cache.h"

#define PATTERN(p) {.pattern = p, .regex = {0}, .compiled = false}

#define SONGLINK_API_BASE_URL ("https://api.song.link/v1-alpha.1/links?url=")

typedef struct {
    const char *pattern;
    regex_t regex;
    bool compiled;
} Pattern;

static Pattern SPOTIFY_PATTERNS[] = {
    PATTERN("open\\.spotify\\.com/(track|album|playlist)/([a-zA-Z0-9]+)"),
    {0},
};

static Pattern YOUTUBE_PATTERNS[] = {
    PATTERN("youtube\\.com/watch\\?v=([a-zA-Z0-9_-]+)"),
    PATTERN("youtu\\.be/([a-zA-Z0-9_-]+)"),
    PATTERN("music\\.youtube\\.com/watch\\?v=([a-zA-Z0-9_-]+)"),
    {0},
};

static Pattern APPLE_MUSIC_PATTERNS[] = {
    PATTERN("music\\.apple\\.com/[a-z]{2}/(album|playlist|song)/[^/]+/([0-9]+)"),
    {0},
};

static Pattern TIDAL_PATTERNS[] = {
    PATTERN("listen\\.tidal\\.com/track/([0-9]+)"),
    PATTERN("tidal\\.com/(album/[0-9]+/)?track/([0-9]+)/?u?"),
    {0},
};

static Pattern SOUNDCLOUD_PATTERNS[] = {
    PATTERN("soundcloud.com/([a-zA-Z0-9_-]+)/([a-zA-Z0-9_-]+)"),
    {0},
};

static Pattern *PLATFORM_PATTERNS[] = {
    [PLATFORM_SPOTIFY] = SPOTIFY_PATTERNS,
    [PLATFORM_YOUTUBE] = YOUTUBE_PATTERNS,
    [PLATFORM_APPLE_MUSIC] = APPLE_MUSIC_PATTERNS,
    [PLATFORM_TIDAL] = TIDAL_PATTERNS,
    [PLATFORM_SOUNDCLOUD] = SOUNDCLOUD_PATTERNS,
};

bool links_init() {
    for (int i = 0; i < PLATFORM_COUNT; i++) {
        for (Pattern *p = PLATFORM_PATTERNS[i]; p->pattern != NULL; p++) {
            if (regcomp(&p->regex, p->pattern, REG_EXTENDED | REG_ICASE) == 0) {
                p->compiled = true;
            } else {
                return false;
            }
        }
    }

    return true;
}

static bool match_music_link(const char *message, char **out_url, const Pattern *patterns) {
    regmatch_t matches[1];

    for (int i = 0; patterns[i].pattern != NULL; i++) {
        assert(patterns[i].compiled);

        if (regexec(&patterns[i].regex, message, 1, matches, 0) == 0) {
            int start = matches[0].rm_so;
            int end = matches[0].rm_eo;
            size_t len = end - start;
            *out_url = malloc(len + 1);
            if (*out_url) {
                memcpy(*out_url, message + start, len);
                (*out_url)[len] = '\0';
            }
            return true;
        }
    }

    return false;
}

bool is_music_link(const char *message, MusicPlatform *out_platform, char **out_url) {

    for (size_t i = 0; i < PLATFORM_COUNT; i++) {
        if (match_music_link(message, out_url, PLATFORM_PATTERNS[i])) {
            if (out_platform) {
                *out_platform = i;
            }
            return true;
        }
    }

    return false;
}

static void parse_music_links_response(cJSON *response_json, MusicLinksData *out_data) {
    static const char *platform_url_keys[] = {
        [PLATFORM_SPOTIFY] = "spotify",
        [PLATFORM_YOUTUBE] = "youtube",
        [PLATFORM_APPLE_MUSIC] = "appleMusic",
        [PLATFORM_TIDAL] = "tidal",
        [PLATFORM_SOUNDCLOUD] = "soundcloud",
    };

    cJSON *platforms = cJSON_GetObjectItem(response_json, "linksByPlatform");
    if (platforms) {

        for (size_t i = 0; i < PLATFORM_COUNT; i++) {
            cJSON *platform = cJSON_GetObjectItem(platforms, platform_url_keys[i]);
            if (platform) {
                cJSON *url = cJSON_GetObjectItem(platform, "url");
                if (url && url->valuestring) {
                    out_data->urls[i] = strdup(url->valuestring);
                }
            }
        }
    }

    cJSON *entity_id_item = cJSON_GetObjectItem(response_json, "entityUniqueId");
    if (entity_id_item && entity_id_item->valuestring) {
        const char *entity_id = entity_id_item->valuestring;
        cJSON *entities = cJSON_GetObjectItem(response_json, "entitiesByUniqueId");
        if (entities) {
            cJSON *entity = cJSON_GetObjectItem(entities, entity_id);
            if (entity) {
                cJSON *thumb = cJSON_GetObjectItem(entity, "thumbnailUrl");
                if (thumb && thumb->valuestring) {
                    out_data->thumbnail_url = strdup(thumb->valuestring);
                }
            }
        }
    }
}

typedef struct {
    char *music_url;
    MusicPlatform original_platform;
    void *user_data;
    MusicLinksCallback user_cb;
} FetchContext;

static void fetch_callback(HTTPResponse *res, void *user_data) {
    FetchContext *ctx = (FetchContext *)user_data;

    if (res->result != CURLE_OK) {
        fprintf(stderr, "Failed to fetch music links: %s\n", curl_easy_strerror(res->result));
        goto cleanup;
    }

    cJSON *json = cJSON_ParseWithLength((const char *)res->data, res->length);
    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "Failed to parse music links JSON: %s\n", error_ptr);
        }
        goto cleanup;
    }

    MusicLinksData *data = music_links_data_create();
    parse_music_links_response(json, data);
    cJSON_Delete(json);

    for (size_t i = 0; i < PLATFORM_COUNT; i++) {
        if (i == ctx->original_platform || !data->urls[i])
            continue;

        char *url = NULL;
        MusicPlatform platform;
        // Cache the links for each other platform as well
        if (is_music_link(data->urls[i], &platform, &url)) {
            MusicLinks *alt_links = music_links_create(platform, music_links_data_retain(data));
            cache_put(url, alt_links);
            free(url);
        }
    }

    MusicLinks *links = music_links_create(ctx->original_platform, data);
    cache_put(ctx->music_url, links);
    ctx->user_cb(*links, ctx->user_data);

cleanup:
    free(ctx->music_url);
    free(ctx);
    return;
}

typedef struct {
    time_t last_fetch;
    uint32_t counter;
    uint32_t limit;
} RateLimiter;

static bool rate_limit(RateLimiter *limiter) {
    time_t now = time(NULL);

    if (now - limiter->last_fetch >= 60) {
        limiter->last_fetch = now;
        limiter->counter = 0;
    }

    if (limiter->counter >= limiter->limit) {
        printf("Rate limit exceeded. %lds left.\n", 60 - (now - limiter->last_fetch));
        return false;
    }

    limiter->counter++;
    return true;
}

bool fetch_music_links(MuseTransport *ts,
                       const char *music_url,
                       MusicLinksCallback on_done,
                       void *user_data) {
    static char encoded_url[2048];
    static char api_url[4096];
    // Songlink's public API has a limit of 10 requests per minute
    static RateLimiter api_limiter = {0, .limit = 10};

    MusicLinks *hit = cache_get(music_url);
    if (hit) {
        printf("Cache hit for '%s'\n", music_url);
        on_done(*hit, user_data);
        return true;
    }

    if (!rate_limit(&api_limiter)) {
        return false;
    }

    // Determine the original platform from the URL
    MusicPlatform original_platform;
    char *temp_url = NULL;
    if (!is_music_link(music_url, &original_platform, &temp_url)) {
        fprintf(stderr, "Invalid music URL: %s\n", music_url);
        return false;
    }
    free(temp_url);

    transport_url_encode(music_url, encoded_url, sizeof(encoded_url));
    snprintf(api_url, sizeof(api_url), "%s%s", SONGLINK_API_BASE_URL, encoded_url);

    FetchContext *ctx = (FetchContext *)malloc(sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "Failed to allocate fetch context");
        return false;
    }
    ctx->music_url = strdup(music_url);
    if (!ctx->music_url) {
        fprintf(stderr, "Failed to duplicate music URL\n");
        goto cleanup_ctx;
    }
    ctx->original_platform = original_platform;
    ctx->user_data = user_data;
    ctx->user_cb = on_done;
    printf("Fetching '%s' on Songlink API\n", music_url);
    if (!transport_http_get(ts, api_url, fetch_callback, ctx)) {
        goto cleanup_url;
    }
    return true;

cleanup_url:
    free(ctx->music_url);
cleanup_ctx:
    free(ctx);
    return false;
}

MusicLinksData *music_links_data_create(void) {
    MusicLinksData *data = calloc(1, sizeof(*data));
    if (data) {
        data->ref_count = 1;
    }
    return data;
}

void music_links_data_free(MusicLinksData *data) {
    for (size_t i = 0; i < PLATFORM_COUNT; i++) {
        if (data->urls[i]) {
            free(data->urls[i]);
            data->urls[i] = NULL;
        }
    }

    if (data->thumbnail_url) {
        free(data->thumbnail_url);
    }
}

MusicLinksData *music_links_data_retain(MusicLinksData *data) {
    if (data) {
        data->ref_count++;
    }
    return data;
}

void music_links_data_release(MusicLinksData *data) {
    if (!data)
        return;

    data->ref_count--;
    if (data->ref_count <= 0) {
        music_links_data_free(data);
        free(data);
    }
}

MusicLinks *music_links_create(MusicPlatform platform, MusicLinksData *data) {
    MusicLinks *links = malloc(sizeof(*links));
    if (links) {
        links->original_platform = platform;
        links->data = data;
    }
    return links;
}

void music_links_release(MusicLinks *links) {
    if (!links)
        return;

    music_links_data_release(links->data);
    free(links);
}

void links_destroy() {
    for (int i = 0; i < PLATFORM_COUNT; i++) {
        for (Pattern *p = PLATFORM_PATTERNS[i]; p->pattern != NULL; p++) {
            regfree(&p->regex);
        }
    }
}
