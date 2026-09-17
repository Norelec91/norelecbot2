#include "test_paths.hpp"

#include "game.hpp"
#include "storage.hpp"
#include "zodiac.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>

using namespace norelecbot;

namespace {

/* Seconds held, times the boost, times what the house of the day was worth to the holder. */
std::int64_t earnings(std::string_view holder, std::int64_t seconds, std::int64_t now, std::int64_t boost = 1) {
    return seconds * boost * zodiac::percent_for(holder, now) / 100;
}

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

        ClaimResult claim = conquister_claim(storage, 1, "alice", 100, 0, false);
        CHECK(claim.status == ClaimStatus::taken);
        CHECK(claim.previous_username.empty());
        claim = conquister_claim(storage, 2, "bob", 1100, 0, false);
        CHECK(claim.previous_username == "alice");
        CHECK(claim.earned == earnings("alice", 1000, 1100));

        const Leaderboard leaderboard = conquister_leaderboard(storage, 10);
        REQUIRE(leaderboard.entries.size() == 1);
        CHECK(leaderboard.entries[0].username == "alice");
        CHECK(leaderboard.entries[0].score == earnings("alice", 1000, 1100));
        REQUIRE(leaderboard.current);
        CHECK(leaderboard.current->username == "bob");
        CHECK(leaderboard.current->since == 1100);

        std::optional<ConquisterUser> user = conquister_user(storage, "ALICE");
        REQUIRE(user);
        CHECK(user->username == "alice");
        CHECK(user->score == earnings("alice", 1000, 1100));
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

        const std::int64_t alice_score = earnings("alice", 1000, 1100);
        QuoteAddResult addition = quote_add(storage, "alice", "quote di prova", 1000);
        CHECK(addition.status == QuoteAddStatus::added);
        CHECK(addition.available_score == alice_score - 1000);
        CHECK(quote_random(storage) == "quote di prova");

        static_cast<void>(conquister_claim(storage, 1, "alice", 1101, 0, false));
        static_cast<void>(conquister_claim(storage, 2, "bob", 2101, 0, false));
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

TEST_CASE("a balloon defends the holder until it pops") {
    const TestPaths paths{"balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, 0, false));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000, 0, false));
    CHECK(balloon_buy(storage, "alice", 1000, 0, 0).status == BalloonStatus::bought);
    CHECK(balloon_buy(storage, "alice", 0, 0, 0).status == BalloonStatus::already_owned);
    CHECK(balloon_buy(storage, "carol", 1000, 0, 0).status == BalloonStatus::insufficient_score);
    static_cast<void>(conquister_claim(storage, 1, "alice", 2000, 0, false));

    int attempts = 0;
    ClaimResult attack;
    do {
        attack = conquister_claim(storage, 2, "bob", 3000 + attempts, 0, false);
        ++attempts;
        if (attack.status == ClaimStatus::defended) {
            CHECK(attack.previous_username == "alice");
            CHECK(attack.next_chance == 25 * (attempts + 1));
            const auto holder = conquister_user(storage, "alice");
            REQUIRE(holder);
            CHECK(holder->in_conquister);
        }
    } while (attack.status == ClaimStatus::defended && attempts < 8);

    CHECK(attempts <= 4);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
    CHECK(attack.previous_username == "alice");

    const ClaimResult without = conquister_claim(storage, 1, "alice", 9000, 0, false);
    CHECK(without.status == ClaimStatus::taken);
    CHECK_FALSE(without.balloon_popped);
}

TEST_CASE("a penalty blocks the next attempts") {
    const TestPaths paths{"cooldown-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},)"
             << R"("quotes_added":{},"balloons":{},"cooldowns":{"bob":1000}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult blocked = conquister_claim(storage, 2, "bob", 700, 300, false);
    CHECK(blocked.status == ClaimStatus::cooldown);
    CHECK(blocked.penalty_seconds == 300);

    const ClaimResult others = conquister_claim(storage, 3, "carol", 700, 300, false);
    CHECK(others.status == ClaimStatus::taken);

    const ClaimResult expired = conquister_claim(storage, 2, "bob", 1000, 300, false);
    CHECK(expired.status == ClaimStatus::taken);
}

TEST_CASE("a failed balloon attempt hands out the penalty") {
    const TestPaths paths{"cooldown-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, 0, false));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000, 0, false));
    CHECK(balloon_buy(storage, "alice", 1000, 0, 0).status == BalloonStatus::bought);
    static_cast<void>(conquister_claim(storage, 1, "alice", 2000, 0, false));

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 3000, 300, false);
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.penalty_seconds == 300);
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100, 300, false);
        CHECK(again.status == ClaimStatus::cooldown);
        CHECK(again.penalty_seconds == 200);
    } else {
        CHECK(attack.status == ClaimStatus::taken);
        CHECK(attack.balloon_popped);
    }
}

TEST_CASE("the fourth attempt pops the balloon for certain") {
    const TestPaths paths{"balloon-certain-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},)"
             << R"("quotes_added":{},"balloons":{"alice":3}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 10, 0, false);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
    const auto winner = conquister_user(storage, "bob");
    REQUIRE(winner);
    CHECK(winner->in_conquister);
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

TEST_CASE("one player's balloon cannot be popped until it deflates") {
    const TestPaths paths{"shield-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, 0, false));
    static_cast<void>(conquister_claim(storage, 2, "bob", 5000, 0, false));
    const BalloonResult bought = balloon_buy(storage, "alice", 1000, 5000, 3600);
    CHECK(bought.status == BalloonStatus::bought);
    CHECK(bought.shield_seconds == 3600);
    const BalloonResult again = balloon_buy(storage, "alice", 0, 5100, 3600);
    CHECK(again.status == BalloonStatus::already_owned);
    CHECK(again.shield_seconds == 3500);
    static_cast<void>(conquister_claim(storage, 1, "alice", 5200, 0, false));

    const ClaimResult first = conquister_claim(storage, 2, "bob", 5300, 300, false);
    CHECK(first.status == ClaimStatus::defended);
    CHECK(first.previous_username == "alice");
    CHECK(first.shield_seconds == 3300);
    CHECK(first.penalty_seconds == 300);
    CHECK(first.next_chance == 0);

    /* Far more attempts than the four an ordinary balloon survives. */
    for (int attempt = 0; attempt < 20; ++attempt) {
        const ClaimResult attack = conquister_claim(storage, 2, "bob", 6000 + attempt, 0, false);
        CHECK(attack.status == ClaimStatus::defended);
        CHECK(attack.shield_seconds > 0);
    }

    const ClaimResult deflated = conquister_claim(storage, 3, "carol", 8700, 0, false);
    CHECK(deflated.status == ClaimStatus::taken);
    CHECK(deflated.previous_username == "alice");
    CHECK_FALSE(deflated.balloon_popped);

    /* Deflated, so the next one can be bought. */
    CHECK(balloon_buy(storage, "alice", 0, 8700, 3600).status == BalloonStatus::bought);
}

TEST_CASE("two shielded players can pop each other's balloon") {
    const TestPaths paths{"shield-duel-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, 0, true));
    CHECK(balloon_buy(storage, "alice", 0, 0, 3600).status == BalloonStatus::bought);

    /* carol is an ordinary player: she cannot. */
    const ClaimResult refused = conquister_claim(storage, 3, "carol", 100, 0, false);
    CHECK(refused.status == ClaimStatus::defended);
    CHECK(refused.shield_seconds == 3500);

    const ClaimResult popped = conquister_claim(storage, 2, "bob", 200, 0, true);
    CHECK(popped.status == ClaimStatus::taken);
    CHECK(popped.balloon_popped);
    CHECK(popped.previous_username == "alice");

    /* Popped for good: alice has to buy another one. */
    CHECK(balloon_buy(storage, "alice", 0, 300, 3600).status == BalloonStatus::bought);
}

TEST_CASE("a boost multiplies what the hold earns, once") {
    const TestPaths paths{"boost-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, 0, false));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000, 0, false));
    const std::int64_t first_hold = earnings("alice", 1000, 1000);
    CHECK(conquister_user(storage, "alice")->score == first_hold);

    const BoostResult bought = boost_buy(storage, "alice", 1000, 3, 1000);
    CHECK(bought.status == BoostStatus::bought);
    CHECK(bought.available_score == first_hold - 1000);
    CHECK(boost_buy(storage, "alice", 0, 3, 1000).status == BoostStatus::already_owned);

    SUBCASE("it is cashed in when the place is taken away") {
        static_cast<void>(conquister_claim(storage, 1, "alice", 2000, 0, false));
        const ClaimResult kicked = conquister_claim(storage, 2, "bob", 2100, 0, false);
        CHECK(kicked.previous_username == "alice");
        CHECK(kicked.boost_multiplier == 3);
        CHECK(kicked.earned == earnings("alice", 100, 2100, 3));
        CHECK(conquister_user(storage, "alice")->score == first_hold - 1000 + kicked.earned);

        /* Spent: the next hold earns the usual. */
        static_cast<void>(conquister_claim(storage, 1, "alice", 3000, 0, false));
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100, 0, false);
        CHECK(again.boost_multiplier == 0);
        CHECK(again.earned == earnings("alice", 100, 3100));
    }

    SUBCASE("it rules out a balloon while it waits") {
        CHECK(balloon_buy(storage, "alice", 0, 2000, 0).status == BalloonStatus::has_boost);
    }
}

TEST_CASE("a balloon rules out a boost") {
    const TestPaths paths{"boost-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(balloon_buy(storage, "alice", 0, 0, 0).status == BalloonStatus::bought);
    CHECK(boost_buy(storage, "alice", 0, 3, 0).status == BoostStatus::has_balloon);

    CHECK(balloon_buy(storage, "bob", 0, 0, 3600).status == BalloonStatus::bought);
    CHECK(boost_buy(storage, "bob", 0, 3, 100).status == BoostStatus::has_balloon);
    /* Once the hour is over the shield is gone and the boost can be bought. */
    CHECK(boost_buy(storage, "bob", 0, 3, 3700).status == BoostStatus::bought);
}
