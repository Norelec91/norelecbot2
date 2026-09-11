#ifndef NORELECBOT_JSON_HPP
#define NORELECBOT_JSON_HPP

#include <nlohmann/json.hpp>

namespace norelecbot {

/* Keeps object keys in insertion order, like the files and responses have always had. */
using Json = nlohmann::ordered_json;

inline const Json *find_member(const Json &object, const char *key) {
    const auto found = object.find(key);
    return found != object.end() ? &*found : nullptr;
}

inline const Json *find_member(const Json *object, const char *key) {
    return object != nullptr ? find_member(*object, key) : nullptr;
}

}

#endif
