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
    PLATFORM_YOUTUBE,
    PLATFORM_APPLE_MUSIC,
    PLATFORM_TIDAL,
    PLATFORM_SOUNDCLOUD,

    PLATFORM_COUNT
} MusicPlatform;

typedef struct {
    char *spotify_url;
    char *youtube_url;
    char *apple_music_url;
    char *tidal_url;
    char *soundcloud_url;
    char *thumbnail_url;
    int ref_count;
} MusicLinksData;

typedef struct {
    MusicPlatform original_platform;
    MusicLinksData *data;
} MusicLinks;

// Pointers in `link` or only valid within the callback
typedef void (*MusicLinksCallback)(MusicLinks links, void *user_data);

bool is_music_link(const char *message, MusicPlatform *out_platform,
                   char **out_url);
// Returns `true` if fetch issued, `false` if rate limited
bool fetch_music_links(MuseTransport *ts, const char *music_url,
                       MusicLinksCallback on_done, void *user_data);
// Assumes data has 
MusicLinks *music_links_create(MusicPlatform platform, MusicLinksData *data);
void music_links_release(MusicLinks *links);
MusicLinksData *music_links_data_create(void);
// NOTE: This function does not retain `data`
void music_links_data_free(MusicLinksData *data);
MusicLinksData *music_links_data_retain(MusicLinksData *data);
void music_links_data_release(MusicLinksData *data);

#endif // LINKS_H
