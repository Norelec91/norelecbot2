#include "test_paths.hpp"

#include "game.hpp"
#include "rest_routes.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <vector>

using namespace norelecbot;

namespace {

void write_quotes(const std::string &path, const std::vector<std::string> &quotes) {
    std::ofstream file{path, std::ios::binary};
    file << Json(quotes).dump(2);
    file.close();
    REQUIRE(file.good());
}

void expect(Storage &storage, std::string_view path, unsigned int status_code, std::string_view body) {
    const RestResponse response = rest_route_dispatch(storage, "GET", path);
    CHECK(response.status_code == status_code);
    CHECK(response.body == body);
}

}

TEST_CASE("the REST routes answer with the documented JSON") {
    const TestPaths paths{"rest-route-test"};
    write_quotes(paths.quotes, {});
    Storage storage{paths.conquister, paths.quotes};

    expect(storage, "/health", 200, "{\"status\":\"ok\"}\n");
    expect(storage, "/missing", 404, "{\"error\":\"not found\"}\n");
    CHECK(rest_route_dispatch(storage, "POST", "/quote").status_code == 404);

    const RestResponse empty = rest_route_dispatch(storage, "GET", "/quote");
    CHECK(empty.status_code == 404);
    CHECK(empty.body.contains("no quotes available"));
    write_quotes(paths.quotes, {"quote di prova"});
    expect(storage, "/quote", 200, "{\"quote\":\"quote di prova\"}\n");

    expect(storage, "/leaderboard", 200, "{\"entries\":[],\"current\":null}\n");
    static_cast<void>(conquister_claim(storage, 7, "Norelec", 100, 0));
    expect(storage, "/leaderboard", 200, "{\"entries\":[],\"current\":{\"username\":\"Norelec\",\"since\":100}}\n");
    static_cast<void>(conquister_claim(storage, 8, "bob", 150, 0));
    expect(
        storage,
        "/leaderboard",
        200,
        "{\"entries\":[{\"rank\":1,\"username\":\"Norelec\",\"score\":50,\"quotes_added\":0}],"
        "\"current\":{\"username\":\"bob\",\"since\":150}}\n"
    );
    expect(
        storage,
        "/user/norelec",
        200,
        "{\"username\":\"Norelec\",\"score\":50,\"rank\":1,\"quotes_added\":0,\"in_conquister\":false}\n"
    );
    expect(
        storage,
        "/user/@bob",
        200,
        "{\"username\":\"bob\",\"score\":0,\"rank\":null,\"quotes_added\":0,"
        "\"in_conquister\":true,\"since\":150}\n"
    );
    expect(storage, "/user/nessuno", 404, "{\"error\":\"user not found\"}\n");
    expect(storage, "/user/", 404, "{\"error\":\"not found\"}\n");
    CHECK(rest_route_dispatch(storage, "GET", "/user/bob/extra").status_code == 404);
    CHECK(rest_route_dispatch(storage, "POST", "/user/bob").status_code == 404);
}
