#include "test_paths.hpp"

#include "game.hpp"
#include "storage.hpp"
#include "position.hpp"
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

        ClaimResult claim = conquister_claim(storage, 1, "alice", 100);
        CHECK(claim.status == ClaimStatus::taken);
        CHECK(claim.previous_username.empty());
        claim = conquister_claim(storage, 2, "bob", 1100);
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

TEST_CASE("a balloon defends the holder until it pops") {
    const TestPaths paths{"balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    CHECK(balloon_buy(storage, "alice", 1000, 0, 0).status == BalloonStatus::bought);
    CHECK(balloon_buy(storage, "alice", 0, 0, 0).status == BalloonStatus::already_owned);
    CHECK(balloon_buy(storage, "carol", 1000, 0, 0).status == BalloonStatus::insufficient_score);
    static_cast<void>(conquister_claim(storage, 1, "alice", 2000));

    int attempts = 0;
    ClaimResult attack;
    do {
        attack = conquister_claim(storage, 2, "bob", 3000 + attempts);
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

    const ClaimResult without = conquister_claim(storage, 1, "alice", 9000);
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

    const ClaimResult blocked = conquister_claim(storage, 2, "bob", 700, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(blocked.status == ClaimStatus::cooldown);
    CHECK(blocked.penalty_seconds == 300);

    const ClaimResult others = conquister_claim(storage, 3, "carol", 700, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(others.status == ClaimStatus::taken);

    const ClaimResult expired = conquister_claim(storage, 2, "bob", 1000, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(expired.status == ClaimStatus::taken);
}

TEST_CASE("a failed balloon attempt hands out the penalty") {
    const TestPaths paths{"cooldown-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    CHECK(balloon_buy(storage, "alice", 1000, 0, 0).status == BalloonStatus::bought);
    static_cast<void>(conquister_claim(storage, 1, "alice", 2000));

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 3000, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.penalty_seconds == 300);
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
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

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 10);
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

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 5000));
    const BalloonResult bought = balloon_buy(storage, "alice", 1000, 5000, 3600);
    CHECK(bought.status == BalloonStatus::bought);
    CHECK(bought.shield_seconds == 3600);
    const BalloonResult again = balloon_buy(storage, "alice", 0, 5100, 3600);
    CHECK(again.status == BalloonStatus::already_owned);
    CHECK(again.shield_seconds == 3500);
    static_cast<void>(conquister_claim(storage, 1, "alice", 5200));

    const ClaimResult first = conquister_claim(storage, 2, "bob", 5300, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(first.status == ClaimStatus::defended);
    CHECK(first.previous_username == "alice");
    CHECK(first.shield_seconds == 3300);
    CHECK(first.penalty_seconds == 300);
    CHECK(first.next_chance == 0);

    /* Far more attempts than the four an ordinary balloon survives. */
    for (int attempt = 0; attempt < 20; ++attempt) {
        const ClaimResult attack = conquister_claim(storage, 2, "bob", 6000 + attempt);
        CHECK(attack.status == ClaimStatus::defended);
        CHECK(attack.shield_seconds > 0);
    }

    const ClaimResult deflated = conquister_claim(storage, 3, "carol", 8700);
    CHECK(deflated.status == ClaimStatus::taken);
    CHECK(deflated.previous_username == "alice");
    CHECK_FALSE(deflated.balloon_popped);

    /* Deflated, so the next one can be bought. */
    CHECK(balloon_buy(storage, "alice", 0, 8700, 3600).status == BalloonStatus::bought);
}

TEST_CASE("two shielded players can pop each other's balloon") {
    const TestPaths paths{"shield-duel-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, ClaimRules{.cooldown_seconds = 0, .attack_cost = 0, .ignores_shield = true, .signs = {}}));
    CHECK(balloon_buy(storage, "alice", 0, 0, 3600).status == BalloonStatus::bought);

    /* carol is an ordinary player: she cannot. */
    const ClaimResult refused = conquister_claim(storage, 3, "carol", 100);
    CHECK(refused.status == ClaimStatus::defended);
    CHECK(refused.shield_seconds == 3500);

    const ClaimResult popped = conquister_claim(storage, 2, "bob", 200, ClaimRules{.cooldown_seconds = 0, .attack_cost = 0, .ignores_shield = true, .signs = {}});
    CHECK(popped.status == ClaimStatus::taken);
    CHECK(popped.balloon_popped);
    CHECK(popped.previous_username == "alice");

    /* Popped for good: alice has to buy another one. */
    CHECK(balloon_buy(storage, "alice", 0, 300, 3600).status == BalloonStatus::bought);
}

TEST_CASE("a boost multiplies what the hold earns, once") {
    const TestPaths paths{"boost-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    const std::int64_t first_hold = earnings("alice", 1000, 1000);
    CHECK(conquister_user(storage, "alice")->score == first_hold);

    const BoostResult bought = boost_buy(storage, "alice", 1000, 3, 1000);
    CHECK(bought.status == BoostStatus::bought);
    CHECK(bought.available_score == first_hold - 1000);
    CHECK(boost_buy(storage, "alice", 0, 3, 1000).status == BoostStatus::already_owned);

    SUBCASE("it is cashed in when the place is taken away") {
        static_cast<void>(conquister_claim(storage, 1, "alice", 2000));
        const ClaimResult kicked = conquister_claim(storage, 2, "bob", 2100);
        CHECK(kicked.previous_username == "alice");
        CHECK(kicked.boost_multiplier == 3);
        CHECK(kicked.earned == earnings("alice", 100, 2100, 3));
        CHECK(conquister_user(storage, "alice")->score == first_hold - 1000 + kicked.earned);

        /* Spent: the next hold earns the usual. */
        static_cast<void>(conquister_claim(storage, 1, "alice", 3000));
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100);
        CHECK(again.boost_multiplier == 0);
        CHECK(again.earned == earnings("alice", 100, 3100));
    }

    SUBCASE("it rules out a balloon while it waits") {
        CHECK(balloon_buy(storage, "alice", 0, 2000, 0).status == BalloonStatus::has_boost);
    }
}

TEST_CASE("a boost cannot be bought retroactively during the current hold") {
    const TestPaths paths{"boost-current-hold-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},)"
             << R"("scores":{"alice":2000,"bob":0},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const BoostResult refused = boost_buy(storage, "alice", 1500, 3, 3599);
    CHECK(refused.status == BoostStatus::holding_place);
    CHECK(refused.available_score == 2000);
    CHECK(conquister_user(storage, "alice")->score == 2000);
    const Json saved = read_json(paths.conquister);
    const bool has_boost = saved.contains("boosts") && !saved.at("boosts").empty();
    CHECK_FALSE(has_boost);

    const RaidResult left = raid_start(storage, 1, "alice", "alice", 3600, RaidRules{});
    CHECK(left.status == RaidStatus::left_place);
    CHECK(left.boost_multiplier == 0);
    CHECK(left.earned == earnings("alice", 3600, 3600));

    CHECK(boost_buy(storage, "alice", 1500, 3, 3601).status == BoostStatus::bought);
    CHECK(conquister_claim(storage, 1, "alice", 4000).status == ClaimStatus::taken);
    const ClaimResult next = conquister_claim(storage, 2, "bob", 4100);
    CHECK(next.boost_multiplier == 3);
    CHECK(next.earned == earnings("alice", 100, 4100, 3));
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

TEST_CASE("a raid shield costs palle and is replaced only by a paid balloon or boost") {
    const TestPaths paths{"raid-shield-buy-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1200},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    CHECK(raid_shield_buy(storage, "bob", 1000, 0).status == RaidShieldStatus::insufficient_score);
    const RaidShieldResult bought = raid_shield_buy(storage, "alice", 1000, 0);
    CHECK(bought.status == RaidShieldStatus::bought);
    CHECK(bought.available_score == 200);
    CHECK(conquister_user(storage, "alice")->score == 200);
    CHECK(raid_shield_buy(storage, "alice", 0, 0).status == RaidShieldStatus::already_owned);
    CHECK(balloon_buy(storage, "alice", 201, 0, 0).status == BalloonStatus::insufficient_score);
    CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);
    CHECK(balloon_buy(storage, "alice", 200, 0, 0).status == BalloonStatus::bought);
    CHECK(conquister_user(storage, "alice")->score == 0);
    CHECK_FALSE(read_json(paths.conquister).at("raid_shields").contains("alice"));
    CHECK(raid_shield_buy(storage, "alice", 0, 0).status == RaidShieldStatus::has_balloon);

    CHECK(balloon_buy(storage, "carol", 0, 0, 0).status == BalloonStatus::bought);
    CHECK(raid_shield_buy(storage, "carol", 0, 0).status == RaidShieldStatus::has_balloon);
    CHECK(balloon_buy(storage, "dave", 0, 0, 3600).status == BalloonStatus::bought);
    CHECK(raid_shield_buy(storage, "dave", 0, 100).status == RaidShieldStatus::has_balloon);
    CHECK(raid_shield_buy(storage, "dave", 0, 3700).status == RaidShieldStatus::bought);
    CHECK(boost_buy(storage, "erin", 0, 3, 0).status == BoostStatus::bought);
    CHECK(raid_shield_buy(storage, "erin", 0, 0).status == RaidShieldStatus::has_boost);
    CHECK(raid_shield_buy(storage, "frank", 0, 0).status == RaidShieldStatus::bought);
    CHECK(boost_buy(storage, "frank", 1, 3, 0).status == BoostStatus::insufficient_score);
    CHECK(read_json(paths.conquister).at("raid_shields").at("frank") == 1);
    CHECK(boost_buy(storage, "frank", 0, 3, 0).status == BoostStatus::bought);
    CHECK_FALSE(read_json(paths.conquister).at("raid_shields").contains("frank"));
    CHECK(raid_shield_buy(storage, "frank", 0, 0).status == RaidShieldStatus::has_boost);
}

TEST_CASE("an attempt that a balloon survives costs palle") {
    const TestPaths paths{"attack-cost-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{"bob":250},)"
             << R"("quotes_added":{},"balloons":{"alice":0},"cooldowns":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult attack =
        conquister_claim(storage, 2, "bob", 10, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .ignores_shield = false, .signs = {}});
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.attack_cost == 100);
        CHECK(conquister_user(storage, "bob")->score == 150);
    } else {
        /* It popped straight away, and a pop costs nothing. */
        CHECK(attack.attack_cost == 0);
        CHECK(conquister_user(storage, "bob")->score == 250);
    }
}

TEST_CASE("nobody is charged more than they have") {
    const TestPaths paths{"attack-cost-empty-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{"bob":30},)"
             << R"("quotes_added":{},"balloons":{},"cooldowns":{},"shields":{"alice":9999}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult first =
        conquister_claim(storage, 2, "bob", 10, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .ignores_shield = false, .signs = {}});
    CHECK(first.status == ClaimStatus::defended);
    CHECK(first.attack_cost == 30);
    CHECK(conquister_user(storage, "bob")->score == 0);

    const ClaimResult second =
        conquister_claim(storage, 2, "bob", 20, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .ignores_shield = false, .signs = {}});
    CHECK(second.attack_cost == 0);
    CHECK(conquister_user(storage, "bob")->score == 0);
}

namespace {

/* Far enough apart that every ride is the shortest one, so the tests do not depend on where ids land. */
RaidRules quick_rides() {
    return RaidRules{.loot_divisor = 50, .loot_share = 3, .travel_divisor = 1000000, .attack_cost = 100, .signs = {}};
}

RaidRules shield_rides() {
    return RaidRules{.loot_divisor = 1, .loot_share = 10, .travel_divisor = 1000000, .signs = {}};
}

}

TEST_CASE("a raid shield reduces the potential loot by x/(x+1000) on every raid") {
    const TestPaths paths{"raid-shield-loot-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":11000,"bob":0},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":5000}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    CHECK(raid_shield_buy(storage, "alice", 1000, 0).status == RaidShieldStatus::bought);
    CHECK(raid_start(storage, 0, "bob", "alice", 0, shield_rides()).status == RaidStatus::started);

    const std::vector<RaidEvent> first = raid_due(storage, 5, shield_rides());
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == RaidEvent::Kind::stolen);
    CHECK(first[0].distance == 5000);
    CHECK(first[0].loot == 500); /* raw 1000, then floor(1000 * 1000 / (1000 + 1000)). */
    CHECK(first[0].shield_absorbed == 500);
    CHECK(conquister_user(storage, "alice")->score == 9500);
    CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);
    const std::vector<RaidEvent> home = raid_due(storage, 10, shield_rides());
    REQUIRE(home.size() == 1);
    CHECK(home[0].loot == 500);
    CHECK(conquister_user(storage, "bob")->score == 500);

    CHECK(raid_start(storage, 0, "bob", "alice", 11, shield_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> second = raid_due(storage, 16, shield_rides());
    REQUIRE(second.size() == 1);
    CHECK(second[0].loot == 231); /* raw 950, shield leaves 462, then planet resistance halves it. */
    CHECK(second[0].shield_absorbed == 488);
    CHECK(second[0].resistance_absorbed == 231);
    CHECK(conquister_user(storage, "alice")->score == 9269);
    CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);
}

TEST_CASE("a raid shield stays ready while its owner is away") {
    const TestPaths paths{"raid-shield-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":2000,"bob":0,"carol":1000},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":500,"carol":1000}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    CHECK(raid_shield_buy(storage, "alice", 1000, 0).status == RaidShieldStatus::bought);
    CHECK(raid_start(storage, 0, "alice", "carol", 0, shield_rides()).status == RaidStatus::started);
    CHECK(raid_start(storage, 0, "bob", "alice", 0, shield_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> arrivals = raid_due(storage, 5, shield_rides());
    REQUIRE(arrivals.size() == 2);
    CHECK(arrivals[1].raider == "bob");
    CHECK(arrivals[1].undefended);
    CHECK(arrivals[1].loot == 100);
    CHECK(arrivals[1].shield_absorbed == 0);
    CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);
}

TEST_CASE("holding the Conquister leaves the shield on the player's own planet unguarded") {
    const TestPaths paths{"raid-shield-holder-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},)"
             << R"("scores":{"alice":2000,"bob":0},"quotes_added":{},)"
             << R"("raid_shields":{"alice":1},"ids":{"alice":0,"bob":5000}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    CHECK(raid_start(storage, 0, "bob", "alice", 0, shield_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> first = raid_due(storage, 5, shield_rides());
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == RaidEvent::Kind::stolen);
    CHECK(first[0].undefended);
    CHECK(first[0].loot == 200);
    CHECK(first[0].shield_absorbed == 0);
    CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);

    static_cast<void>(raid_due(storage, 10, shield_rides()));
    CHECK(raid_start(storage, 1, "alice", "alice", 11, shield_rides()).status == RaidStatus::left_place);
    CHECK(raid_start(storage, 0, "bob", "alice", 12, shield_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> second = raid_due(storage, 17, shield_rides());
    REQUIRE(second.size() == 1);
    CHECK_FALSE(second[0].undefended);
    CHECK(second[0].shield_absorbed > 0);
    CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);
}

TEST_CASE("holding the Conquister also leaves the balloon on the player's own planet unguarded") {
    const TestPaths paths{"raid-balloon-holder-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},)"
             << R"("scores":{"alice":2000,"bob":0},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"ids":{"alice":0,"bob":5000}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    CHECK(raid_start(storage, 0, "bob", "alice", 0, shield_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> first = raid_due(storage, 5, shield_rides());
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == RaidEvent::Kind::stolen);
    CHECK(first[0].undefended);
    CHECK_FALSE(first[0].balloon_popped);
    CHECK(read_json(paths.conquister).at("balloons").at("alice") == 3);

    static_cast<void>(raid_due(storage, 10, shield_rides()));
    CHECK(raid_start(storage, 1, "alice", "alice", 11, shield_rides()).status == RaidStatus::left_place);
    CHECK(raid_start(storage, 0, "bob", "alice", 12, shield_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> second = raid_due(storage, 17, shield_rides());
    REQUIRE(second.size() == 1);
    CHECK_FALSE(second[0].undefended);
    CHECK(second[0].balloon_popped); /* The fourth attempt is certain to pop it. */
}

TEST_CASE("a raid shield rounds down and safely handles large loot") {
    SUBCASE("one potential palla is stopped completely") {
        const TestPaths paths{"raid-shield-one-test"};
        {
            std::ofstream file{paths.conquister, std::ios::binary};
            file << R"({"current":null,"scores":{"alice":1,"bob":0},"quotes_added":{},)"
                 << R"("ids":{"alice":0,"bob":500}})";
        }
        Storage storage{paths.conquister, paths.quotes};
        CHECK(raid_shield_buy(storage, "alice", 0, 0).status == RaidShieldStatus::bought);
        RaidRules rules = shield_rides();
        rules.loot_share = 0;
        CHECK(raid_start(storage, 0, "bob", "alice", 0, rules).status == RaidStatus::started);
        const std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
        REQUIRE(arrival.size() == 1);
        CHECK(arrival[0].loot == 0);
        CHECK(arrival[0].shield_absorbed == 1);
        CHECK_FALSE(read_json(paths.conquister).at("raid_resistance_levels").contains("alice"));
        CHECK(conquister_user(storage, "alice")->score == 1);
        CHECK(read_json(paths.conquister).at("raid_shields").at("alice") == 1);
    }

    SUBCASE("large potential loot does not overflow the multiplication") {
        const TestPaths paths{"raid-shield-large-test"};
        {
            std::ofstream file{paths.conquister, std::ios::binary};
            file << R"({"current":null,"scores":{"alice":1000000000000,"bob":0},"quotes_added":{},)"
                 << R"("ids":{"alice":0,"bob":50000}})";
        }
        Storage storage{paths.conquister, paths.quotes};
        CHECK(raid_shield_buy(storage, "alice", 0, 0).status == RaidShieldStatus::bought);
        RaidRules rules = shield_rides();
        rules.loot_share = 0;
        CHECK(raid_start(storage, 0, "bob", "alice", 0, rules).status == RaidStatus::started);
        const std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
        REQUIRE(arrival.size() == 1);
        const std::int64_t potential = arrival[0].distance * arrival[0].raider_percent /
            arrival[0].target_percent;
        CHECK(potential > 1000);
        CHECK(arrival[0].loot == potential * potential / (potential + 1000));
        CHECK(arrival[0].shield_absorbed == potential - arrival[0].loot);
        CHECK(conquister_user(storage, "alice")->score == 1000000000000 - arrival[0].loot);
    }
}

TEST_CASE("planet resistance follows the victim across attackers, persists, and recovers") {
    const TestPaths paths{"raid-planet-resistance-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000000,"bob":0,"carol":0},)"
             << R"("ids":{"alice":0,"bob":5000,"carol":4000}})";
    }
    RaidRules rules = shield_rides();
    rules.loot_share = 0;
    const auto potential = [](const RaidEvent &event) {
        return event.distance * event.raider_percent / event.target_percent;
    };
    {
        Storage storage{paths.conquister, paths.quotes};
        REQUIRE(raid_start(storage, 0, "bob", "alice", 0, rules).status == RaidStatus::started);
        const auto first = raid_due(storage, 5, rules);
        REQUIRE(first.size() == 1);
        CHECK(first[0].loot == potential(first[0]));
        CHECK(first[0].resistance_absorbed == 0);
        CHECK(read_json(paths.conquister).at("raid_resistance_levels").at("alice") == 1);
        CHECK(read_json(paths.conquister).at("raid_resistance_since").at("alice") == 5);
        static_cast<void>(raid_due(storage, 10, rules));
    }
    {
        Storage storage{paths.conquister, paths.quotes};
        REQUIRE(raid_start(storage, 0, "carol", "alice", 11, rules).status == RaidStatus::started);
        const auto second = raid_due(storage, 16, rules);
        REQUIRE(second.size() == 1);
        CHECK(second[0].loot == potential(second[0]) / 2);
        CHECK(second[0].resistance_absorbed == potential(second[0]) - second[0].loot);
        static_cast<void>(raid_due(storage, 21, rules));

        REQUIRE(raid_start(storage, 0, "bob", "alice", 22, rules).status == RaidStatus::started);
        const auto third = raid_due(storage, 27, rules);
        REQUIRE(third.size() == 1);
        CHECK(third[0].loot == potential(third[0]) / 4);
        static_cast<void>(raid_due(storage, 32, rules));

        REQUIRE(raid_start(storage, 0, "bob", "alice", 33, rules).status == RaidStatus::started);
        const auto fourth = raid_due(storage, 38, rules);
        REQUIRE(fourth.size() == 1);
        CHECK(fourth[0].loot == potential(fourth[0]) / 8);
        static_cast<void>(raid_due(storage, 43, rules));

        REQUIRE(raid_start(storage, 0, "bob", "alice", 7200, rules).status == RaidStatus::started);
        const auto recovering = raid_due(storage, 7205, rules);
        REQUIRE(recovering.size() == 1);
        CHECK(recovering[0].loot == potential(recovering[0]) / 4);
        static_cast<void>(raid_due(storage, 7210, rules));

        REQUIRE(raid_start(storage, 0, "bob", "alice", 28800, rules).status == RaidStatus::started);
        const auto recovered = raid_due(storage, 28805, rules);
        REQUIRE(recovered.size() == 1);
        CHECK(recovered[0].loot == potential(recovered[0]));
        CHECK(recovered[0].resistance_absorbed == 0);
    }
}

TEST_CASE("a raid takes a quarter of what the target has, and carries it home") {
    const TestPaths paths{"raid-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":40},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const RaidResult start = raid_start(storage, 7, "bob", "ALICE", 0, quick_rides());
    CHECK(start.status == RaidStatus::started);
    CHECK(start.target == "alice");
    CHECK(start.seconds == position::shortest_travel);

    CHECK(raid_start(storage, 7, "bob", "alice", 1, quick_rides()).status == RaidStatus::already_travelling);
    CHECK(raid_start(storage, 0, "carol", "carol", 1, quick_rides()).status == RaidStatus::home_already);
    CHECK(raid_start(storage, 0, "carol", "nessuno", 1, quick_rides()).status == RaidStatus::unknown_target);
    CHECK(raid_due(storage, 4, quick_rides()).empty());

    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::stolen);
    CHECK(arrival[0].raider == "bob");
    CHECK(arrival[0].target == "alice");
    /* A palla for every unit of road, but never more than a third of what the target owns: one
       raid alone leaves nobody at nothing. */
    CHECK(arrival[0].distance > 0);
    const std::int64_t carried = (arrival[0].distance / 50) *
        zodiac::percent_for("bob", 5) / zodiac::percent_for("alice", 5);
    const std::int64_t loot = std::min(carried, std::int64_t{1000} / 3);
    CHECK(arrival[0].loot == loot);
    CHECK(conquister_user(storage, "alice")->score > 0);
    /* alice has never written to the bot from Telegram, so her name carries no mention. */
    CHECK_FALSE(arrival[0].target_on_telegram);
    /* Taken from the target at once, handed over only at the end of the ride. */
    CHECK(conquister_user(storage, "alice")->score == 1000 - loot);
    CHECK(conquister_user(storage, "bob")->score == 40);

    CHECK(raid_due(storage, 9, quick_rides()).empty());
    const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
    REQUIRE(home.size() == 1);
    CHECK(home[0].kind == RaidEvent::Kind::returned);
    CHECK(home[0].loot == loot);
    CHECK(conquister_user(storage, "bob")->score == 40 + loot);
    /* Home again, so he can leave again. */
    CHECK(raid_start(storage, 7, "bob", "alice", 11, quick_rides()).status == RaidStatus::started);
}

TEST_CASE("a balloon turns a raid back") {
    const TestPaths paths{"raid-balloon-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":500},"quotes_added":{},)"
             << R"("balloons":{"alice":3}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* The fourth attempt pops it for certain, so this raid gets through. */
    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::stolen);
    CHECK(arrival[0].balloon_popped);
    CHECK(arrival[0].loot > 0);

    SUBCASE("a fresh balloon can send him home empty handed") {
        static_cast<void>(raid_due(storage, 10, quick_rides()));
        CHECK(balloon_buy(storage, "alice", 0, 10, 0).status == BalloonStatus::bought);
        static_cast<void>(raid_start(storage, 0, "bob", "alice", 11, quick_rides()));
        const std::vector<RaidEvent> second = raid_due(storage, 16, quick_rides());
        REQUIRE(second.size() == 1);
        if (second[0].kind == RaidEvent::Kind::defended) {
            CHECK(second[0].cost == 100);
            CHECK(second[0].loot == 0);
            const std::vector<RaidEvent> back = raid_due(storage, 21, quick_rides());
            REQUIRE(back.size() == 1);
            CHECK(back[0].kind == RaidEvent::Kind::returned);
            CHECK(back[0].loot == 0);
        } else {
            CHECK(second[0].balloon_popped);
        }
    }
}

TEST_CASE("an empty house has no defences") {
    const TestPaths paths{"raid-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":0,)"
             << R"("carol":800},"quotes_added":{},"balloons":{"alice":0}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* alice leaves to rob carol, so her own balloon guards nothing. */
    static_cast<void>(raid_start(storage, 0, "alice", "carol", 0, quick_rides()));
    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));

    const std::vector<RaidEvent> arrivals = raid_due(storage, 5, quick_rides());
    REQUIRE(arrivals.size() == 2);
    for (const RaidEvent &event : arrivals) {
        CHECK(event.kind == RaidEvent::Kind::stolen);
        if (event.raider == "bob") {
            CHECK(event.undefended);
            CHECK_FALSE(event.balloon_popped);
        }
    }
    /* The balloon is still hers, it simply was not at home either. */
    CHECK(balloon_buy(storage, "alice", 0, 5, 0).status == BalloonStatus::already_owned);
}

TEST_CASE("nobody takes the place from the road") {
    const TestPaths paths{"raid-claim-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":100,"carol":10},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(raid_start(storage, 0, "alice", "carol", 0, quick_rides()));
    const ClaimResult refused = conquister_claim(storage, 1, "alice", 1);
    CHECK(refused.status == ClaimStatus::travelling);
    CHECK(refused.travel_seconds == 9);
    CHECK_FALSE(conquister_user(storage, "alice")->in_conquister);

    /* Home again, and the place is his to take. */
    static_cast<void>(raid_due(storage, 10, quick_rides()));
    CHECK(conquister_claim(storage, 1, "alice", 11).status == ClaimStatus::taken);
}

TEST_CASE("an id is drawn once and stays") {
    const TestPaths paths{"raid-id-test"};
    std::int64_t drawn = 0;
    {
        Storage storage{paths.conquister, paths.quotes};
        static_cast<void>(conquister_claim(storage, 1, "alice", 0));
        static_cast<void>(conquister_claim(storage, 2, "bob", 10));
        const RaidResult first = raid_start(storage, 0, "alice", "bob", 20, quick_rides());
        CHECK(first.status == RaidStatus::started);
        drawn = first.seconds;
    }

    const Json state = read_json(paths.conquister);
    CHECK(state.at("telegram_ids").at("alice").get<std::int64_t>() == 1);
    CHECK(state.at("telegram_ids").at("bob").get<std::int64_t>() == 2);
    REQUIRE(state.at("ids").is_object());
    CHECK(state.at("ids").size() == 2);
    const std::int64_t alice = state.at("ids").at("alice").get<std::int64_t>();
    CHECK(alice >= 0);
    CHECK(alice < position::ids);
    CHECK(state.at("ids").at("bob").get<std::int64_t>() != alice);

    Storage reopened{paths.conquister, paths.quotes};
    static_cast<void>(raid_due(reopened, 1000, quick_rides()));
    const RaidResult again = raid_start(reopened, 0, "alice", "bob", 1000, quick_rides());
    CHECK(again.seconds == drawn);
    CHECK(read_json(paths.conquister).at("ids").at("alice").get<std::int64_t>() == alice);
}

TEST_CASE("whoever holds the place does not leave it") {
    const TestPaths paths{"raid-holder-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{"alice":900,"bob":10},)"
             << R"("quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const RaidResult refused = raid_start(storage, 1, "ALICE", "bob", 1, quick_rides());
    CHECK(refused.status == RaidStatus::holding_place);
    /* Naming herself is how she comes back down. */
    CHECK(raid_start(storage, 1, "alice", "alice", 1, quick_rides()).status == RaidStatus::left_place);
    CHECK(conquister_user(storage, "alice")->score >= 900);

    /* Out of the place, free to go. */
    static_cast<void>(conquister_claim(storage, 2, "bob", 2));
    CHECK(raid_start(storage, 1, "alice", "bob", 3, quick_rides()).status == RaidStatus::started);
}

TEST_CASE("naming yourself is the way home") {
    const TestPaths paths{"raid-home-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":700},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    SUBCASE("at home it changes nothing") {
        const RaidResult already = raid_start(storage, 0, "alice", "ALICE", 0, quick_rides());
        CHECK(already.status == RaidStatus::home_already);
        CHECK(conquister_user(storage, "alice")->score == 1000);
    }

    SUBCASE("from the place it is instant, and the hold is cashed in") {
        static_cast<void>(conquister_claim(storage, 1, "alice", 100));
        const RaidResult left = raid_start(storage, 1, "alice", "alice", 400, quick_rides());
        CHECK(left.status == RaidStatus::left_place);
        CHECK(left.earned == 300 * zodiac::percent_for("alice", 400) / 100);
        CHECK(conquister_user(storage, "alice")->score == 1000 + left.earned);
        /* The place is empty now, and she can leave on a raid. */
        CHECK_FALSE(conquister_leaderboard(storage, 10).current);
        CHECK(raid_start(storage, 1, "alice", "bob", 401, quick_rides()).status == RaidStatus::started);
    }

    SUBCASE("on the road it calls the raid off and rides back the way it came") {
        static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
        /* Two seconds out, so two seconds back. */
        const RaidResult back = raid_start(storage, 0, "bob", "bob", 2, quick_rides());
        CHECK(back.status == RaidStatus::coming_home);
        CHECK(back.seconds == 2);

        /* Nothing is stolen at the hour he would have arrived. */
        CHECK(conquister_user(storage, "alice")->score == 1000);

        const std::vector<RaidEvent> home = raid_due(storage, 4, quick_rides());
        REQUIRE(home.size() == 1);
        CHECK(home[0].kind == RaidEvent::Kind::returned);
        CHECK(home[0].loot == 0);
        CHECK(conquister_user(storage, "bob")->score == 700);
    }
}

TEST_CASE("a quote remembers who added it") {
    const TestPaths paths{"quote-author-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(quote_add(storage, "alice", "una citazione", 0).status == QuoteAddStatus::added);
    CHECK(quote_add(storage, "bob", "un'altra", 0).status == QuoteAddStatus::added);

    QuotePage page = quote_page_load(storage, 1);
    REQUIRE(page.authors.size() == 2);
    CHECK(page.authors[0] == "alice");
    CHECK(page.authors[1] == "bob");
    CHECK(read_json(paths.conquister).at("quote_authors").at("una citazione") == "alice");

    /* Deleting a quote forgets its author and takes it off his count. */
    CHECK(conquister_user(storage, "alice")->quotes_added == 1);
    CHECK(quote_delete(storage, "1") == "una citazione");
    CHECK(conquister_user(storage, "alice")->quotes_added == 0);
    page = quote_page_load(storage, 1);
    REQUIRE(page.authors.size() == 1);
    CHECK(page.authors[0] == "bob");
    CHECK(read_json(paths.conquister).at("quote_authors").size() == 1);

    /* A quote nobody is known to have added takes nothing off anybody. */
    {
        std::ofstream file{paths.quotes, std::ios::binary};
        file << R"(["senza autore","un'altra"])";
    }
    {
        Storage orphan{paths.conquister, paths.quotes};
        CHECK(quote_delete(orphan, "1") == "senza autore");
        CHECK(conquister_user(orphan, "bob")->quotes_added == 1);
    }

    /* A quote from before the bot wrote it down has no author, and that is not a hole. */
    {
        std::ofstream file{paths.quotes, std::ios::binary};
        file << R"(["vecchia","un'altra"])";
    }
    Storage reopened{paths.conquister, paths.quotes};
    page = quote_page_load(reopened, 1);
    REQUIRE(page.authors.size() == 2);
    CHECK(page.authors[0].empty());
    CHECK(page.authors[1] == "bob");
}


TEST_CASE("furniture is bought, piles up and stops at the limit") {
    const TestPaths paths{"furniture-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":25000,"bob":100},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* The first purchase takes the cost and hangs the emoji. */
    const FurnitureResult first = furniture_buy(storage, "alice", "🎈🍕", 10000, 10);
    CHECK(first.status == FurnitureStatus::bought);
    CHECK(first.shown == "🎈🍕");
    CHECK(first.howmany == 2);
    CHECK(conquister_user(storage, "alice")->score == 15000);

    /* The second one is added to them instead of replacing them. */
    const FurnitureResult second = furniture_buy(storage, "alice", "🐟", 10000, 10);
    CHECK(second.status == FurnitureStatus::bought);
    CHECK(second.shown == "🎈🍕🐟");
    CHECK(second.howmany == 3);
    CHECK(conquister_user(storage, "alice")->score == 5000);

    /* Past the limit it refuses without charging. */
    const FurnitureResult too_many = furniture_buy(storage, "alice", "🚀🎲🧀🐝🌊🪐🎺🐕", 10000, 10);
    CHECK(too_many.status == FurnitureStatus::too_many);
    CHECK(too_many.howmany == 3);
    CHECK(conquister_user(storage, "alice")->score == 5000);

    /* With no palle nothing is bought and nothing is left hanging. */
    const FurnitureResult broke = furniture_buy(storage, "bob", "🎈", 10000, 10);
    CHECK(broke.status == FurnitureStatus::insufficient_score);
    CHECK(conquister_user(storage, "bob")->score == 100);
    const Authors hung = furniture_all(storage);
    CHECK(hung.find("bob") == hung.end());

    /* And what was bought survives a trip through the file. */
    Storage again{paths.conquister, paths.quotes};
    const Authors kept = furniture_all(again);
    REQUIRE(kept.find("alice") != kept.end());
    CHECK(kept.find("alice")->second == "🎈🍕🐟");
}

TEST_CASE("turning back mid journey only costs the road already walked") {
    const TestPaths paths{"turnback-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":9000,"bob":9000},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    /* A divisor of one turns the distance itself into seconds, so the legs are long enough to
       turn back in the middle of one. */
    const RaidRules slow{.loot_divisor = 50, .loot_share = 0, .travel_divisor = 1, .attack_cost = 100, .signs = {}};

    const RaidResult left = raid_start(storage, 7, "bob", "alice", 0, slow);
    REQUIRE(left.status == RaidStatus::started);
    const std::int64_t leg = left.seconds;
    REQUIRE(leg > 10);

    /* A third of the way there, he changes his mind. */
    const std::int64_t third = leg / 3;
    const RaidResult back = raid_start(storage, 7, "bob", "bob", third, slow);
    CHECK(back.status == RaidStatus::coming_home);
    CHECK(back.seconds == third);

    /* Nothing is stolen, and he is home after exactly the road he had walked. */
    CHECK(raid_due(storage, third + third - 1, slow).empty());
    const std::vector<RaidEvent> home = raid_due(storage, third + third, slow);
    REQUIRE(home.size() == 1);
    CHECK(home[0].kind == RaidEvent::Kind::returned);
    CHECK(home[0].loot == 0);
    CHECK(conquister_user(storage, "alice")->score == 9000);

    /* And once home he can leave again. */
    CHECK(raid_start(storage, 7, "bob", "alice", third + third, slow).status == RaidStatus::started);
}

TEST_CASE("investments compound daily, remain separate from score, and survive a restart") {
    const TestPaths paths{"investment-service-test"};
    constexpr std::int64_t day = 86400;
    {
        Storage storage{paths.conquister, paths.quotes};
        const std::string alice = player_seen(storage, 1, "Alice");
        const std::string bob = player_seen(storage, 2, "Bob");
        storage.transaction([&](StorageSession &session) {
            session.state().scores[alice] = 3000;
            return 0;
        });
        CHECK(investment_deposit(storage, alice, "Bob", RaidTargetKind::telegram, 1000, 0).status ==
              InvestmentStatus::not_self);
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::irc, 1000, 0).status ==
              InvestmentStatus::not_self);
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 0, 0).status ==
              InvestmentStatus::invalid_amount);
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 4000, 0).status ==
              InvestmentStatus::insufficient_score);
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, 0).status ==
              InvestmentStatus::deposited);
        CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 2000);
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, day).status ==
              InvestmentStatus::deposited);
        CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);
        CHECK(investment_withdraw(storage, bob, "Alice", RaidTargetKind::telegram, day).status ==
              InvestmentStatus::not_self);
    }
    {
        Storage storage{paths.conquister, paths.quotes};
        const InvestmentResult payout = investment_withdraw(storage, "tg:1", "Alice", RaidTargetKind::telegram,
                                                              2 * day);
        CHECK(payout.status == InvestmentStatus::withdrawn);
        CHECK(payout.amount == 2090);
        CHECK(payout.interest == 90);
        CHECK(payout.score == 3090);
        CHECK(investment_withdraw(storage, "tg:1", "Alice", RaidTargetKind::telegram, 2 * day).status ==
              InvestmentStatus::no_investment);
        CHECK(read_json(paths.conquister).at("investments").empty());
    }
}

TEST_CASE("investment withdrawal and deposit require the player's planet") {
    const TestPaths paths{"investment-location-test"};
    Storage storage{paths.conquister, paths.quotes};
    const std::string alice = player_seen(storage, 1, "Alice");
    const std::string bob = player_seen(storage, 2, "Bob");
    storage.transaction([&](StorageSession &session) {
        session.state().scores[alice] = 2000;
        return 0;
    });
    CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, 0).status ==
          InvestmentStatus::deposited);
    CHECK(raid_start(storage, 1, alice, "Bob", 1, {}, RaidTargetKind::telegram).status == RaidStatus::started);
    CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1, 2).status ==
          InvestmentStatus::not_home);
    CHECK(investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram, 2).status ==
          InvestmentStatus::not_home);
    storage.transaction([&](StorageSession &session) {
        session.state().raids.clear();
        session.state().current = Holder{.user_id = 1, .username = alice, .since = 3};
        return 0;
    });
    CHECK(investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram, 4).status ==
          InvestmentStatus::not_home);
    storage.transaction([&](StorageSession &session) {
        session.state().current.reset();
        return 0;
    });
    CHECK(investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram, 86400).status ==
          InvestmentStatus::withdrawn);
}

TEST_CASE("raids can steal only the non-invested balance") {
    const TestPaths paths{"investment-raid-test"};
    Storage storage{paths.conquister, paths.quotes};
    const std::string alice = player_seen(storage, 1, "Alice");
    const std::string bob = player_seen(storage, 2, "Bob");
    storage.transaction([&](StorageSession &session) {
        session.state().scores[alice] = 2000;
        return 0;
    });
    REQUIRE(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, 0).status ==
            InvestmentStatus::deposited);
    const RaidRules rules{.loot_divisor = 1, .loot_share = 0, .travel_divisor = 1000,
                          .attack_cost = 0, .signs = {}};
    const RaidResult trip = raid_start(storage, 2, bob, "Alice", 1, rules, RaidTargetKind::telegram);
    REQUIRE(trip.status == RaidStatus::started);
    const std::vector<RaidEvent> arrival = raid_due(storage, 1 + trip.seconds, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].loot <= 1000);
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000 - arrival[0].loot);
    const InvestmentResult payout = investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram,
                                                         1 + trip.seconds);
    CHECK(payout.status == InvestmentStatus::withdrawn);
    CHECK(payout.amount >= 1000);
    CHECK(payout.score >= 1000);
}
