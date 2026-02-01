#include "links.h"

#include <regex.h>
#include <stdio.h>
#include <string.h>

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

static char encoded_url[2048];
static char api_url[4096];

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

bool is_music_link(const char *message, char **out_url) {
    if (match_music_link(message, out_url, SPOTIFY_PATTERNS)) {
        return true;
    }
    if (match_music_link(message, out_url, YOUTUBE_PATTERNS)) {
        return true;
    }
    if (match_music_link(message, out_url, APPLE_MUSIC_PATTERNS)) {
        return true;
    }
    return false;
}

void fetch_music_links(MuseTransport *ts, const char *music_url,
                       HTTPCallback on_done, void *user_data) {
    transport_url_encode(music_url, encoded_url, sizeof(encoded_url));
    snprintf(api_url, sizeof(api_url), "%s%s", SONGLINK_API_BASE_URL,
             encoded_url);
    transport_http_get(ts, api_url, on_done, user_data);
}

void parse_music_links_response(cJSON *response_json, MusicLinks *out_links) {
    cJSON *platforms = cJSON_GetObjectItem(response_json, "linksByPlatform");
    if (platforms) {
        cJSON *spotify = cJSON_GetObjectItem(platforms, "spotify");
        if (spotify) {
            cJSON *url = cJSON_GetObjectItem(spotify, "url");
            if (url && url->valuestring) {
                out_links->spotify_url = strdup(url->valuestring);
            }
        }

        cJSON *youtube = cJSON_GetObjectItem(platforms, "youtube");
        if (youtube) {
            cJSON *url = cJSON_GetObjectItem(youtube, "url");
            if (url && url->valuestring) {
                out_links->youtube_url = strdup(url->valuestring);
            }
        }

        cJSON *apple = cJSON_GetObjectItem(platforms, "appleMusic");
        if (apple) {
            cJSON *url = cJSON_GetObjectItem(apple, "url");
            if (url && url->valuestring) {
                out_links->apple_music_url = strdup(url->valuestring);
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
                    out_links->thumbnail_url = strdup(thumb->valuestring);
                }
            }
        }
    }
}

void music_links_free(MusicLinks *links) {
    if (links->spotify_url) {
        free(links->spotify_url);
        links->spotify_url = NULL;
    }
    if (links->youtube_url) {
        free(links->youtube_url);
        links->youtube_url = NULL;
    }
    if (links->apple_music_url) {
        free(links->apple_music_url);
        links->apple_music_url = NULL;
    }
    if (links->thumbnail_url) {
        free(links->thumbnail_url);
        links->thumbnail_url = NULL;
    }
}
