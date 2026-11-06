#ifndef UTHASH_MAP_H
#define UTHASH_MAP_H

#include <stdbool.h>

struct uthash_map;

/*
 * Store a string value under a string key. Values are borrowed, not copied.
 * If b_malloc_key is false, the key is borrowed too and must remain valid
 * until the map is freed; otherwise the map owns a copy of the key.
 */
int uthash_map_set(struct uthash_map **map, const char *key, const char *value,
                   bool b_malloc_key);
const char *uthash_map_get(struct uthash_map *map, const char *key);
void uthash_map_free(struct uthash_map *map);

#endif
