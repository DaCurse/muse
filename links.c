#include "links.h"

#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cache.h"

#define SONGLINK_API_BASE_URL ("https://api.song.link/v1-alpha.1/links?url=")

const char *SPOTIFY_PATTERNS[] = {
    "open\\.spotify\\.com/(track|album|playlist)/([a-zA-Z0-9]+)",
    "spotify:track:([a-zA-Z0-9]+)",
    NULL,
};

const char *YOUTUBE_PATTERNS[] = {
    "youtube\\.com/watch\\?v=([a-zA-Z0-9_-]+)",
    "youtu\\.be/([a-zA-Z0-9_-]+)",
    "music\\.youtube\\.com/watch\\?v=([a-zA-Z0-9_-]+)",
    NULL,
};

const char *APPLE_MUSIC_PATTERNS[] = {
    "music\\.apple\\.com/[a-z]{2}/(album|playlist|song)/[^/]+/([0-9]+)",
    NULL,
};

const char *TIDAL_PATTERNS[] = {
    "listen\\.tidal\\.com/track/([0-9]+)",
    "tidal\\.com/(album/[0-9]+/)?track/([0-9]+)/?u?",
    NULL,
};

const char *SOUNDCLOUD_PATTERNS[] = {
    "soundcloud.com/([a-zA-Z0-9_-]+)/([a-zA-Z0-9_-]+)",
    NULL,
};

static bool match_music_link(const char *message, char **out_url,
                             const char **patterns) {
    regex_t regex;
    regmatch_t matches[1];

    for (int i = 0; patterns[i] != NULL; i++) {
        if (regcomp(&regex, patterns[i], REG_EXTENDED | REG_ICASE) == 0) {
            if (regexec(&regex, message, 1, matches, 0) == 0) {
                int start = matches[0].rm_so;
                int end = matches[0].rm_eo;
                size_t len = end - start;
                *out_url = malloc(len + 1);
                if (*out_url) {
                    memcpy(*out_url, message + start, len);
                    (*out_url)[len] = '\0';
                }
                regfree(&regex);
                return true;
            }
            regfree(&regex);
        }
    }
    return false;
}

typedef struct {
    const char **patterns;
    MusicPlatform platform;
} PlatformPatternMapping;

bool is_music_link(const char *message, MusicPlatform *out_platform,
                   char **out_url) {
    PlatformPatternMapping platform_checks[] = {
        {SPOTIFY_PATTERNS, PLATFORM_SPOTIFY},
        {YOUTUBE_PATTERNS, PLATFORM_YOUTUBE},
        {APPLE_MUSIC_PATTERNS, PLATFORM_APPLE_MUSIC},
        {TIDAL_PATTERNS, PLATFORM_TIDAL},
        {SOUNDCLOUD_PATTERNS, PLATFORM_SOUNDCLOUD},
    };

    for (size_t i = 0; i < sizeof(platform_checks) / sizeof(platform_checks[0]);
         i++) {
        if (match_music_link(message, out_url, platform_checks[i].patterns)) {
            if (out_platform) {
                *out_platform = platform_checks[i].platform;
            }
            return true;
        }
    }
    return false;
}

typedef struct {
    const char *key;
    char **dest;
} PlatformKeyLinkMapping;

static void parse_music_links_response(cJSON *response_json,
                                       MusicLinksData *out_data) {
    cJSON *platforms = cJSON_GetObjectItem(response_json, "linksByPlatform");
    if (platforms) {
        PlatformKeyLinkMapping platform_map[] = {
            {"spotify", &out_data->spotify_url},
            {"youtube", &out_data->youtube_url},
            {"appleMusic", &out_data->apple_music_url},
            {"tidal", &out_data->tidal_url},
            {"soundcloud", &out_data->soundcloud_url},
        };

        for (size_t i = 0; i < sizeof(platform_map) / sizeof(platform_map[0]);
             i++) {
            cJSON *platform =
                cJSON_GetObjectItem(platforms, platform_map[i].key);
            if (platform) {
                cJSON *url = cJSON_GetObjectItem(platform, "url");
                if (url && url->valuestring) {
                    *platform_map[i].dest = strdup(url->valuestring);
                }
            }
        }
    }

    cJSON *entity_id_item =
        cJSON_GetObjectItem(response_json, "entityUniqueId");
    if (entity_id_item && entity_id_item->valuestring) {
        const char *entity_id = entity_id_item->valuestring;
        cJSON *entities =
            cJSON_GetObjectItem(response_json, "entitiesByUniqueId");
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

typedef struct {
    char *link;
    MusicPlatform platform;
} PlatformLinkMapping;

static void fetch_callback(HTTPResponse *res, void *user_data) {
    FetchContext *ctx = (FetchContext *)user_data;

    if (res->result != CURLE_OK) {
        fprintf(stderr, "Failed to fetch music links: %s\n",
                curl_easy_strerror(res->result));
        goto cleanup;
    }

    cJSON *json = cJSON_ParseWithLength((const char *)res->data, res->length);
    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "Failed to parse music links JSON: %s\n",
                    error_ptr);
        }
        goto cleanup;
    }

    MusicLinksData *data = music_links_data_create();
    parse_music_links_response(json, data);
    cJSON_Delete(json);

    PlatformLinkMapping platform_map[] = {
        {data->spotify_url, PLATFORM_SPOTIFY},
        {data->youtube_url, PLATFORM_YOUTUBE},
        {data->apple_music_url, PLATFORM_APPLE_MUSIC},
        {data->tidal_url, PLATFORM_TIDAL},
        {data->soundcloud_url, PLATFORM_SOUNDCLOUD},
    };

    for (size_t i = 0; i < sizeof(platform_map) / sizeof(platform_map[0]);
         i++) {
        if (platform_map[i].platform == ctx->original_platform ||
            platform_map[i].link == NULL) {
            continue;
        }

        char *url = NULL;
        MusicPlatform platform;
        // Cache the links for each other platform as well
        if (is_music_link(platform_map[i].link, &platform, &url)) {
            MusicLinks *alt_links =
                music_links_create(platform, music_links_data_retain(data));
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
        printf("Rate limit exceeded. %lds left.\n",
               60 - (now - limiter->last_fetch));
        return false;
    }

    limiter->counter++;
    return true;
}

bool fetch_music_links(MuseTransport *ts, const char *music_url,
                       MusicLinksCallback on_done, void *user_data) {
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
    snprintf(api_url, sizeof(api_url), "%s%s", SONGLINK_API_BASE_URL,
             encoded_url);

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
    char **urls[] = {
        &data->spotify_url, &data->youtube_url,    &data->apple_music_url,
        &data->tidal_url,   &data->soundcloud_url, &data->thumbnail_url,
    };

    for (size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
        if (*urls[i]) {
            free(*urls[i]);
            *urls[i] = NULL;
        }
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
