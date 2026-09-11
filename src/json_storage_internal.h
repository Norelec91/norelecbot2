#ifndef NORELECBOT_JSON_STORAGE_INTERNAL_H
#define NORELECBOT_JSON_STORAGE_INTERNAL_H

#include "storage.h"

#include <jansson.h>
#include <stdint.h>

/* Services delimit transactions with lock/unlock; load, save and RNG require that lock. */
[[nodiscard]] bool json_storage_lock(Storage *storage);
void json_storage_unlock(Storage *storage);

[[nodiscard]] json_t *json_storage_load_conquister(Storage *storage);
[[nodiscard]] json_t *json_storage_load_quotes(Storage *storage);
[[nodiscard]] bool json_storage_save_conquister(const Storage *storage, json_t *state);
[[nodiscard]] bool json_storage_save_quotes(const Storage *storage, json_t *quotes);
[[nodiscard]] uint64_t json_storage_next_quote_random(Storage *storage);

[[nodiscard]] int64_t json_integer_member(json_t *object, const char *name, int64_t fallback);
[[nodiscard]] bool json_set_integer(json_t *object, const char *name, int64_t value);

#endif
