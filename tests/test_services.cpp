#include "test_paths.hpp"

#include "game.hpp"
#include "storage.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>

using namespace norelecbot;

namespace {

Json read_json(const std::string &path) {
    std::ifstream file{path, std::ios::binary};
    return Json::parse(file);
}

}

TEST_CASE("claims, leaderboard, users and quotes") {
    const TestPaths paths{"telegram-service-test"};
    {
        Storage storage{paths.conquister, paths.quotes};
        CHECK(quote_page_load(storage, 1).total == 0);
        CHECK_FALSE(quote_random(storage));

        ClaimResult claim = conquister_claim(storage, 1, "alice", 100);
        CHECK(claim.status == ClaimStatus::taken);
        CHECK(claim.previous_username.empty());
        claim = conquister_claim(storage, 2, "bob", 1100);
        CHECK(claim.previous_username == "alice");
        CHECK(claim.earned == 1000);

        const Leaderboard leaderboard = conquister_leaderboard(storage, 10);
        REQUIRE(leaderboard.entries.size() == 1);
        CHECK(leaderboard.entries[0].username == "alice");
        CHECK(leaderboard.entries[0].score == 1000);
        REQUIRE(leaderboard.current);
        CHECK(leaderboard.current->username == "bob");
        CHECK(leaderboard.current->since == 1100);

        std::optional<ConquisterUser> user = conquister_user(storage, "ALICE");
        REQUIRE(user);
        CHECK(user->username == "alice");
        CHECK(user->score == 1000);
        CHECK(user->rank == 1);
        CHECK(user->quotes_added == 0);
        CHECK_FALSE(user->in_conquister);
        user = conquister_user(storage, "bob");
        REQUIRE(user);
        CHECK(user->in_conquister);
        CHECK(user->since == 1100);
        CHECK(user->score == 0);
        CHECK(user->rank == 0);
        CHECK_FALSE(conquister_user(storage, "carol"));

        QuoteAddResult addition = quote_add(storage, "alice", "quote di prova", 1000);
        CHECK(addition.status == QuoteAddStatus::added);
        CHECK(addition.available_score == 0);
        CHECK(quote_random(storage) == "quote di prova");

        static_cast<void>(conquister_claim(storage, 1, "alice", 1101));
        static_cast<void>(conquister_claim(storage, 2, "bob", 2101));
        addition = quote_add(storage, "alice", "quote di prova", 1000);
        CHECK(addition.status == QuoteAddStatus::duplicate);
        const QuotePage page = quote_page_load(storage, 1);
        REQUIRE(page.items.size() == 1);
        CHECK(page.items[0] == "quote di prova");

        CHECK(quote_delete(storage, "1") == "quote di prova");
        CHECK_FALSE(quote_delete(storage, "1"));
        CHECK(quote_page_load(storage, 1).total == 0);
    }

    SUBCASE("the saved files keep their shape and reload") {
        const Json state = read_json(paths.conquister);
        CHECK(state.is_object());
        CHECK(state.at("current").is_object());
        CHECK(state.at("scores").is_object());
        CHECK(state.at("quotes_added").is_object());
        CHECK(read_json(paths.quotes).is_array());

        Storage storage{paths.conquister, paths.quotes};
        Leaderboard leaderboard = conquister_leaderboard(storage, 10);
        REQUIRE_FALSE(leaderboard.entries.empty());
        CHECK(leaderboard.entries[0].username == "alice");
        CHECK(conquister_leaderboard(storage, 1).entries.size() == 1);
        leaderboard = conquister_leaderboard(storage, 0);
        REQUIRE(leaderboard.entries.size() == 2);
        CHECK(leaderboard.entries[1].username == "bob");
    }
}

TEST_CASE("a quote saved before a failed state save is rolled back") {
    const TestPaths paths{"rollback-test"};
    {
        std::ofstream file{paths.quotes, std::ios::binary};
        file << "[\"originale\"]";
    }
    Storage storage{"rollback-missing-directory/conquister.json", paths.quotes};
    CHECK_THROWS_AS(static_cast<void>(quote_add(storage, "alice", "nuova", 0)), const StorageError &);

    const QuotePage page = quote_page_load(storage, 1);
    CHECK(page.total == 1);
    REQUIRE(page.items.size() == 1);
    CHECK(page.items[0] == "originale");
}
