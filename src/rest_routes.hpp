#ifndef NORELECBOT_REST_ROUTES_HPP
#define NORELECBOT_REST_ROUTES_HPP

#include "storage.hpp"

#include <string>
#include <string_view>

namespace norelecbot {

struct RestResponse {
    unsigned int status_code = 500;
    std::string body;
};

/* Produces a complete HTTP response, including 404; internal failures throw. */
[[nodiscard]] RestResponse rest_route_dispatch(Storage &storage, std::string_view method, std::string_view path);

}

#endif
