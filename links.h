#ifndef LINKS_H
#define LINKS_H

#include <stdbool.h>
#include <stdlib.h>

#include <cjson/cJSON.h>

#include "transport.h"

extern const char *SPOTIFY_PATTERNS[];
extern const char *YOUTUBE_PATTERNS[];
extern const char *APPLE_MUSIC_PATTERNS[];
extern const char *TIDAL_PATTERNS[];
extern const char *SOUNDCLOUD_PATTERNS[];

typedef enum {
    PLATFORM_SPOTIFY,
    PLATFORM_YOUTBUE,
    PLATFORM_APPLE_MUSIC,
    PLATFORM_TIDAL,
    PLATFORM_SOUNDCLOUD,
} MusicPlatform;

typedef struct {
    MusicPlatform original;
    char *spotify_url;
    char *youtube_url;
    char *apple_music_url;
    char *tidal_url;
    char *soundcloud_url;
    char *thumbnail_url;
} MusicLinks;

bool is_music_link(const char *message, MusicPlatform *out_platform,
                   char **out_url);
void fetch_music_links(MuseTransport *ts, const char *music_url,
                       HTTPCallback on_done, void *user_data);
void parse_music_links_response(cJSON *response_json, MusicLinks *out_links);
void music_links_free(MusicLinks *links);

#endif // LINKS_H
