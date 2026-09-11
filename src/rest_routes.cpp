#include "rest_routes.hpp"

#include "game.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace norelecbot {
namespace {

using RouteHandler = RestResponse (*)(Storage &storage, std::string_view argument);

/* A path ending in '/' matches as a prefix and passes the rest of the URL as argument. */
struct RestRoute {
    std::string_view method;
    std::string_view path;
    RouteHandler handler;
};

RestResponse text_response(unsigned int status_code, std::string_view body) {
    return {status_code, std::string{body}};
}

RestResponse json_response(const Json &root) {
    return {200, root.dump() + '\n'};
}

RestResponse user_response(const ConquisterUser &user) {
    Json root{
        {"username", user.username},
        {"score", user.score},
        {"rank", user.rank > 0 ? Json(user.rank) : Json(nullptr)},
        {"quotes_added", user.quotes_added},
        {"in_conquister", user.in_conquister},
    };
    if (user.in_conquister) {
        root["since"] = user.since;
    }
    return json_response(root);
}

RestResponse leaderboard_response(const Leaderboard &leaderboard) {
    Json entries = Json::array();
    std::size_t rank = 0;
    for (const LeaderboardEntry &entry : leaderboard.entries) {
        entries.push_back(Json{
            {"rank", ++rank},
            {"username", entry.username},
            {"score", entry.score},
            {"quotes_added", entry.quotes_added},
        });
    }
    Json current = leaderboard.current
        ? Json{{"username", leaderboard.current->username}, {"since", leaderboard.current->since}}
        : Json(nullptr);
    return json_response(Json{{"entries", std::move(entries)}, {"current", std::move(current)}});
}

RestResponse handle_health(Storage &, std::string_view) {
    return text_response(200, "{\"status\":\"ok\"}\n");
}

RestResponse handle_quote(Storage &storage, std::string_view) {
    const std::optional<std::string> quote = quote_random(storage);
    if (!quote) {
        return text_response(404, "{\"error\":\"no quotes available\"}\n");
    }
    return json_response(Json{{"quote", *quote}});
}

RestResponse handle_leaderboard(Storage &storage, std::string_view) {
    return leaderboard_response(conquister_leaderboard(storage, 0));
}

RestResponse handle_user(Storage &storage, std::string_view argument) {
    const std::string_view username = argument.starts_with('@') ? argument.substr(1) : argument;
    if (username.empty() || username.contains('/')) {
        return text_response(404, "{\"error\":\"not found\"}\n");
    }
    const std::optional<ConquisterUser> user = conquister_user(storage, username);
    if (!user) {
        return text_response(404, "{\"error\":\"user not found\"}\n");
    }
    return user_response(*user);
}

constexpr std::array routes{
    RestRoute{"GET", "/health", handle_health},
    RestRoute{"GET", "/quote", handle_quote},
    RestRoute{"GET", "/leaderboard", handle_leaderboard},
    RestRoute{"GET", "/user/", handle_user},
};

}

RestResponse rest_route_dispatch(Storage &storage, std::string_view method, std::string_view path) {
    for (const RestRoute &route : routes) {
        if (method != route.method) {
            continue;
        }
        if (route.path.ends_with('/')) {
            if (path.starts_with(route.path)) {
                return route.handler(storage, path.substr(route.path.size()));
            }
        } else if (path == route.path) {
            return route.handler(storage, {});
        }
    }
    return text_response(404, "{\"error\":\"not found\"}\n");
}

}
