#include "cache.h"

#include <assert.h>
#include <stdio.h>

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
    if (slot->value) {
        printf("Evicted cached value at index %zu", index);
        free(slot->value);
    }

    printf("Cached '%s' at index %zu", key, index);
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

    pos += snprintf(buffer + pos, buffer_size - pos, "```\n");

    pos += snprintf(buffer + pos, buffer_size - pos,
                    "CACHE STATS\n"
                    "Used: %zu/%zu (%.1f%%)\n\n",
                    used, CACHE_SIZE, (used * 100.0) / CACHE_SIZE);

    if (used == 0) {
        pos += snprintf(buffer + pos, buffer_size - pos, "Cache is empty\n```");
        return;
    }

    if (pos >= buffer_size - 100)
        goto close_block;

    size_t empty_start = (size_t)-1;
    size_t entries_shown = 0;
    const size_t max_entries = 25;

    for (size_t i = 0; i <= CACHE_SIZE; i++) {
        bool occupied = (i < CACHE_SIZE && cache[i].value != NULL);

        if (!occupied && empty_start == (size_t)-1) {
            empty_start = i;
        }

        if (occupied) {
            if (empty_start != (size_t)-1) {
                // Print empty range
                if (entries_shown < max_entries && pos < buffer_size - 100) {
                    if (i - empty_start == 1) {
                        pos += snprintf(buffer + pos, buffer_size - pos,
                                        "[%zu] empty\n", empty_start);
                    } else {
                        pos +=
                            snprintf(buffer + pos, buffer_size - pos,
                                     "[%zu-%zu] empty\n", empty_start, i - 1);
                    }
                    entries_shown++;
                }
                empty_start = (size_t)-1;
            }

            if (entries_shown >= max_entries) {
                size_t remaining = 0;
                for (size_t j = i; j < CACHE_SIZE; j++) {
                    if (cache[j].value)
                        remaining++;
                }
                pos += snprintf(buffer + pos, buffer_size - pos,
                                "... %zu more slot%s\n", remaining,
                                remaining == 1 ? "" : "s");
                break;
            }

            if (pos < buffer_size - 100) {
                CacheSlot *slot = &cache[i];
                int links = 0;
                if (slot->value->spotify_url)
                    links++;
                if (slot->value->youtube_url)
                    links++;
                if (slot->value->apple_music_url)
                    links++;
                if (slot->value->tidal_url)
                    links++;
                if (slot->value->soundcloud_url)
                    links++;

                pos += snprintf(buffer + pos, buffer_size - pos,
                                "[%zu] hash=%016llx links=%d\n", i,
                                (unsigned long long)slot->hash, links);
                entries_shown++;
            }
        }
    }

    // Handle trailing empty range
    if (empty_start != (size_t)-1 && entries_shown < max_entries &&
        pos < buffer_size - 100) {
        if (CACHE_SIZE - empty_start == 1) {
            pos += snprintf(buffer + pos, buffer_size - pos, "[%zu] empty\n",
                            empty_start);
        } else {
            pos += snprintf(buffer + pos, buffer_size - pos,
                            "[%zu-%zu] empty\n", empty_start, CACHE_SIZE - 1);
        }
    }

close_block:
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

