#include "cache.h"

#include <assert.h>
#include <stdio.h>

#define CACHE_SIZE (8192)
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
    if (slot->value) {
        printf("Evicted cached value at index %zu\n", index);
        music_links_free(slot->value);
        free(slot->value);
    }

    printf("Cached '%s' at index %zu\n", key, index);
    slot->hash = hash;
    slot->value = value;
}

void cache_summary(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0)
        return;

    size_t used = 0;
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].value) {
            used++;
        }
    }

    size_t pos = 0;
    pos += snprintf(
        buffer + pos, buffer_size - pos,
        "```\nCache: %zu used, %zu empty (%u total, %.1f%% full)\n", used,
        CACHE_SIZE - used, CACHE_SIZE, (used * 100.0) / CACHE_SIZE);

    if (used == 0) {
        snprintf(buffer + pos, buffer_size - pos, "```");
        return;
    }

    // Show up to 40 entries
    size_t shown = 0;
    for (size_t i = 0; i < CACHE_SIZE && shown < 40 && pos < buffer_size - 100;
         i++) {
        if (!cache[i].value)
            continue;

        int links = 0;
        if (cache[i].value->spotify_url)
            links++;
        if (cache[i].value->youtube_url)
            links++;
        if (cache[i].value->apple_music_url)
            links++;
        if (cache[i].value->tidal_url)
            links++;
        if (cache[i].value->soundcloud_url)
            links++;

        pos += snprintf(buffer + pos, buffer_size - pos, "[%zu] %016llx (%d)\n",
                        i, (unsigned long long)cache[i].hash, links);
        shown++;
    }

    if (shown < used) {
        pos += snprintf(buffer + pos, buffer_size - pos,
                        "... +%zu more entries not shown\n", used - shown);
    }

    snprintf(buffer + pos, buffer_size - pos, "```");
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

