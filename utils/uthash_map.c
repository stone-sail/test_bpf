#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "uthash.h"
#include "uthash_map.h"

struct uthash_map {
  const char *key;
  const char *value;
  bool owns_key;
  UT_hash_handle hh;
};

int uthash_map_set(struct uthash_map **map, const char *key, const char *value,
                   bool b_malloc_key)
{
  struct uthash_map *entry;
  size_t key_len;

  if (!map || !key)
    return -1;

  HASH_FIND_STR(*map, key, entry);
  if (entry) {
    entry->value = value;
    return 0;
  }

  entry = malloc(sizeof(*entry));
  if (!entry)
    return -1;
  if (b_malloc_key) {
    key_len = strlen(key) + 1;
    entry->key = malloc(key_len);
    if (!entry->key) {
      free(entry);
      return -1;
    }
    memcpy((char *)entry->key, key, key_len);
    entry->owns_key = true;
  } else {
    entry->key = key;
    entry->owns_key = false;
  }
  entry->value = value;
  HASH_ADD_KEYPTR(hh, *map, entry->key, strlen(key), entry);
  return 0;
}

const char *uthash_map_get(struct uthash_map *map, const char *key)
{
  struct uthash_map *entry;

  if (!map || !key)
    return NULL;

  HASH_FIND_STR(map, key, entry);
  return entry ? entry->value : NULL;
}

void uthash_map_free(struct uthash_map *map)
{
  struct uthash_map *entry, *tmp;

  HASH_ITER(hh, map, entry, tmp) {
    HASH_DEL(map, entry);
    if (entry->owns_key)
      free((void *)entry->key);
    free(entry);
  }
}
