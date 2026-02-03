#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>

#include "links.h"

typedef struct {
    uint64_t hash;
    MusicLinks *value;
} CacheSlot;

MusicLinks *cache_get(const char *key);
void cache_put(const char *key, MusicLinks *value);
void cache_summary(char *buffer, size_t buffer_size);
void cache_destroy();

#endif // CACHE_H
