#include "cache.h"

#include <assert.h>

#define CACHE_SIZE (4096)
#define FNV_64_PRIME (1099511628211ULL)
#define FNV_64_OFFSET_BASIS (14695981039346656037ULL)

static_assert((CACHE_SIZE & (CACHE_SIZE - 1)) == 0,
              "CACHE_SIZE must be a power of 2");

static CacheSlot cache[CACHE_SIZE] = {0};

static uint64_t fnv1a_64_hash(const char *key) {
    uint64_t hash = FNV_64_OFFSET_BASIS;

    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= FNV_64_PRIME;
    }

    return hash;
}

MusicLinks *cache_get(const char *key) {
    if (!key)
        return NULL;

    uint64_t hash = fnv1a_64_hash(key);
    size_t index = hash & (CACHE_SIZE - 1);
    CacheSlot slot = cache[index];

    return slot.hash == hash ? slot.value : NULL;
}

void cache_put(const char *key, MusicLinks *value) {
    if (!key)
        return;

    uint64_t hash = fnv1a_64_hash(key);
    size_t index = hash & (CACHE_SIZE - 1);
    CacheSlot *slot = &cache[index];

    // Cache eviction strategy - index collision!
    if (slot->value)
        free(slot->value);

    slot->hash = hash;
    slot->value = value;
}

void cache_summary(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0)
        return;

    size_t used = 0;
    size_t pos = 0;

    // Count used slots
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].value) {
            used++;
        }
    }

    pos += snprintf(buffer + pos, buffer_size - pos,
                    "Cache: %zu/%zu slots (%.1f%%)\n", used, CACHE_SIZE,
                    (used * 100.0) / CACHE_SIZE);

    if (pos >= buffer_size - 1)
        return;

    pos += snprintf(buffer + pos, buffer_size - pos,
                    "Index          | Hash               | Links\n");

    if (pos >= buffer_size - 1)
        return;

    pos += snprintf(buffer + pos, buffer_size - pos,
                    "---------------|--------------------|-----------\n");

    if (pos >= buffer_size - 1)
        return;

    size_t range_start = (size_t)-1;

    for (size_t i = 0; i <= CACHE_SIZE; i++) {
        bool occupied = (i < CACHE_SIZE && cache[i].value != NULL);

        if (!occupied && range_start != (size_t)-1) {
            // End of empty range, print it
            if (range_start == i - 1) {
                pos += snprintf(buffer + pos, buffer_size - pos,
                                "%-14zu | %-18s | %s\n", range_start, "empty",
                                "-");
            } else {
                pos += snprintf(buffer + pos, buffer_size - pos,
                                "%-14zu | %-18s | %s\n", range_start, "empty",
                                "-");
            }
            range_start = (size_t)-1;

            if (pos >= buffer_size - 1)
                return;
        }

        if (occupied) {
            CacheSlot *slot = &cache[i];

            // Count non-null links
            int link_count = 0;
            if (slot->value->spotify_url)
                link_count++;
            if (slot->value->youtube_url)
                link_count++;
            if (slot->value->apple_music_url)
                link_count++;
            if (slot->value->tidal_url)
                link_count++;
            if (slot->value->soundcloud_url)
                link_count++;

            pos += snprintf(buffer + pos, buffer_size - pos,
                            "%-14zu | 0x%016llx | %d link%s\n", i,
                            (unsigned long long)slot->hash, link_count,
                            link_count == 1 ? "" : "s");

            if (pos >= buffer_size - 1)
                return;
        } else if (range_start == (size_t)-1) {
            // Start of empty range
            range_start = i;
        }
    }

    if (used == 0) {
        pos += snprintf(buffer + pos, buffer_size - pos, "(cache empty)\n");
    }
}

void cache_destroy() {
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        CacheSlot slot = cache[i];
        if (slot.value) {
            music_links_free(slot.value);
            free(slot.value);
        }
    }
}

