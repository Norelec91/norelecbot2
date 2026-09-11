#ifndef NORELECBOT_JSON_STORAGE_INTERNAL_H
#define NORELECBOT_JSON_STORAGE_INTERNAL_H

#include "storage.h"

#include <json-c/json.h>
#include <stdint.h>

/* Services delimit transactions with lock/unlock; load, save and RNG require that lock. */
bool json_storage_lock(Storage *storage);
void json_storage_unlock(Storage *storage);

json_object *json_storage_load_conquister(Storage *storage);
json_object *json_storage_load_quotes(Storage *storage);
bool json_storage_save_conquister(Storage *storage, json_object *state);
bool json_storage_save_quotes(Storage *storage, json_object *quotes);
uint64_t json_storage_next_quote_random(Storage *storage);

json_object *json_member(json_object *object, const char *name);
int64_t json_integer(json_object *object, const char *name, int64_t fallback);
bool json_set_integer(json_object *object, const char *name, int64_t value);

#endif
