#include "test_paths.hpp"

#include "game.hpp"
#include "storage.hpp"
#include "position.hpp"
#include "zodiac.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <chrono>

using namespace norelecbot;

namespace {

/* Seconds held, times the boost, times what the house of the day was worth to the holder. */
std::int64_t earnings(std::string_view holder, std::int64_t seconds, std::int64_t now, std::int64_t boost = 1) {
    return seconds * boost * zodiac::percent_for(holder, now) / 100;
}

int bank_rate(std::string_view holder, std::int64_t now, int magnitude, zodiac::Overrides signs = {}) {
    const int percent = zodiac::percent_for(holder, now, signs);
    return percent == 125 ? magnitude : percent == 75 ? -magnitude : 0;
}

std::string_view matching_sign(zodiac::Element house) {
    return house == zodiac::Element::water ? "cancro" :
        house == zodiac::Element::fire ? "leone" :
        house == zodiac::Element::air ? "gemelli" : "toro";
}

std::string_view opposing_sign(zodiac::Element house) {
    return house == zodiac::Element::water ? "leone" :
        house == zodiac::Element::fire ? "cancro" :
        house == zodiac::Element::air ? "toro" : "gemelli";
}

std::string_view neutral_sign(zodiac::Element house) {
    return house == zodiac::Element::water || house == zodiac::Element::fire ? "gemelli" : "cancro";
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
        storage.transaction([](StorageSession &session) {
            session.state().balloons["alice"] = 3;
            return 0;
        });
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

        storage.transaction([](StorageSession &session) {
            session.state().balloons["bob"] = 3;
            return 0;
        });
        static_cast<void>(conquister_claim(storage, 1, "alice", 1101));
        storage.transaction([](StorageSession &session) {
            session.state().balloons["alice"] = 3;
            return 0;
        });
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

    const ClaimResult entered = conquister_claim(storage, 1, "alice", 0);
    CHECK(entered.status == ClaimStatus::taken);
    CHECK(entered.balloon_active);
    /* Everybody has a balloon: a fresh one leaves no trace on file until something hits it. */
    CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("alice"));

    int attempts = 0;
    ClaimResult attack;
    do {
        attack = conquister_claim(storage, 2, "bob", 1000 + attempts);
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
    /* Popped and kicked out, alice is back to a fresh balloon; bob brought his own, untouched. */
    CHECK(read_json(paths.conquister).at("balloons").empty());

    /* Worn at the place, the balloon goes home with him just as worn: it is the same one. */
    storage.transaction([](StorageSession &session) {
        session.state().balloons["bob"] = 2;
        return 0;
    });
    CHECK(raid_start(storage, 2, "bob", "bob", 9000, RaidRules{}).status == RaidStatus::left_place);
    CHECK(read_json(paths.conquister).at("balloons").at("bob") == 2);
    const ClaimResult again = conquister_claim(storage, 1, "alice", 9001);
    CHECK(again.status == ClaimStatus::taken);
    CHECK(again.balloon_active);
}

TEST_CASE("a penalty blocks the next attempts") {
    const TestPaths paths{"cooldown-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        /* Alice's balloon already took three attempts, so the next one pops it for sure. */
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},)"
             << R"("quotes_added":{},"balloons":{"alice":3},"cooldowns":{"bob":1000}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult blocked = conquister_claim(storage, 2, "bob", 700, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .signs = {}});
    CHECK(blocked.status == ClaimStatus::cooldown);
    CHECK(blocked.penalty_seconds == 300);

    const ClaimResult others = conquister_claim(storage, 3, "carol", 700, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .signs = {}});
    CHECK(others.status == ClaimStatus::taken);
    CHECK(raid_start(storage, 3, "carol", "carol", 900, RaidRules{}).status == RaidStatus::left_place);

    const ClaimResult expired = conquister_claim(storage, 2, "bob", 1000, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .signs = {}});
    CHECK(expired.status == ClaimStatus::taken);
}

TEST_CASE("a failed balloon attempt hands out the penalty") {
    const TestPaths paths{"cooldown-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(conquister_claim(storage, 1, "alice", 0).balloon_active);

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 3000, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .signs = {}});
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.penalty_seconds == 300);
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .signs = {}});
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

TEST_CASE("active legacy timed balloons become ordinary balloons") {
    const TestPaths paths{"legacy-timed-balloon-test"};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << Json{{"current", Json{{"user_id", 1}, {"username", "alice"}, {"since", now - 100}}},
                     {"scores", Json{{"alice", 1000}}},
                     {"shields", Json{{"alice", now + 3600}, {"bob", now - 1}}}}.dump();
    }
    Storage storage{paths.conquister, paths.quotes};
    CHECK_FALSE(read_json(paths.conquister).contains("shields"));
    CHECK(read_json(paths.conquister).at("balloons").at("alice") == 0);
    CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("bob"));
    CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("bob"));
    storage.transaction([](StorageSession &session) {
        session.state().balloons["alice"] = 3;
        return 0;
    });
    const ClaimResult attack = conquister_claim(storage, 2, "bob", now);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
}

TEST_CASE("a boost multiplies what the hold earns, once") {
    const TestPaths paths{"boost-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    CHECK(raid_start(storage, 1, "alice", "alice", 1000, RaidRules{}).status == RaidStatus::left_place);
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    const std::int64_t first_hold = earnings("alice", 1000, 1000);
    CHECK(conquister_user(storage, "alice")->score == first_hold);

    const BoostResult bought = boost_buy(storage, "alice", 1000, 3);
    CHECK(bought.status == BoostStatus::bought);
    CHECK(bought.available_score == first_hold - 1000);
    CHECK(boost_buy(storage, "alice", 0, 3).status == BoostStatus::already_owned);

    SUBCASE("it is cashed in when the place is taken away") {
        CHECK(raid_start(storage, 2, "bob", "bob", 1999, RaidRules{}).status == RaidStatus::left_place);
        static_cast<void>(conquister_claim(storage, 1, "alice", 2000));
        CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("alice"));
        const ClaimResult kicked = conquister_claim(storage, 2, "bob", 2100);
        CHECK(kicked.previous_username == "alice");
        CHECK(kicked.boost_multiplier == 3);
        CHECK(kicked.earned == earnings("alice", 100, 2100, 3));
        CHECK(conquister_user(storage, "alice")->score == first_hold - 1000 + kicked.earned);

        /* Spent: the next hold earns the usual. */
        CHECK(raid_start(storage, 2, "bob", "bob", 2999, RaidRules{}).status == RaidStatus::left_place);
        static_cast<void>(conquister_claim(storage, 1, "alice", 3000));
        storage.transaction([](StorageSession &session) {
            session.state().balloons["alice"] = 3;
            return 0;
        });
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100);
        CHECK(again.boost_multiplier == 0);
        CHECK(again.earned == earnings("alice", 100, 3100));
    }

    SUBCASE("it suppresses the automatic balloon on the next hold") {
        CHECK(raid_start(storage, 2, "bob", "bob", 1999, RaidRules{}).status == RaidStatus::left_place);
        const ClaimResult boosted = conquister_claim(storage, 1, "alice", 2000);
        CHECK(boosted.status == ClaimStatus::taken);
        CHECK_FALSE(boosted.balloon_active);
        /* The boost rules out even the balloon she had brought home from her last hold. */
        CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("alice"));
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

    const BoostResult refused = boost_buy(storage, "alice", 1500, 3);
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

    CHECK(boost_buy(storage, "alice", 1500, 3).status == BoostStatus::bought);
    CHECK(conquister_claim(storage, 1, "alice", 4000).status == ClaimStatus::taken);
    const ClaimResult next = conquister_claim(storage, 2, "bob", 4100);
    CHECK(next.boost_multiplier == 3);
    CHECK(next.earned == earnings("alice", 100, 4100, 3));
}

TEST_CASE("a boost replaces the automatic balloon on entry") {
    const TestPaths paths{"boost-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(boost_buy(storage, "alice", 0, 3).status == BoostStatus::bought);
    CHECK_FALSE(conquister_claim(storage, 1, "alice", 0).balloon_active);
    CHECK(read_json(paths.conquister).at("balloons").empty());
    CHECK(conquister_claim(storage, 2, "bob", 100).balloon_active);
    CHECK(read_json(paths.conquister).at("balloons").empty());
}

TEST_CASE("bought raid shields leave old saves without a refund") {
    const TestPaths paths{"raid-shield-retired-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1200,"bob":40},"quotes_added":{},)"
             << R"("raid_shields":{"alice":1,"bob":1}})";
    }
    /* Opening the storage is enough: the old shields go on the first load. */
    const Storage storage{paths.conquister, paths.quotes};
    const Json saved = read_json(paths.conquister);
    CHECK_FALSE(saved.contains("raid_shields"));
    CHECK(saved.at("scores").at("alice") == 1200);
    CHECK(saved.at("scores").at("bob") == 40);
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
        conquister_claim(storage, 2, "bob", 10, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .signs = {}});
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
             << R"("quotes_added":{},"balloons":{"alice":0},"cooldowns":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult first =
        conquister_claim(storage, 2, "bob", 10, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .signs = {}});
    if (first.status == ClaimStatus::defended) {
        CHECK(first.attack_cost == 30);
        CHECK(conquister_user(storage, "bob")->score == 0);
        const ClaimResult second =
            conquister_claim(storage, 2, "bob", 20, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .signs = {}});
        CHECK(second.attack_cost == 0);
        CHECK(conquister_user(storage, "bob")->score == 0);
    } else {
        CHECK(first.status == ClaimStatus::taken);
        CHECK(first.attack_cost == 0);
        CHECK(conquister_user(storage, "bob")->score == 30);
    }
}

namespace {

/* Far enough apart that every ride is the shortest one, so the tests do not depend on where ids land. */
RaidRules quick_rides() {
    return RaidRules{.loot_divisor = 50, .loot_share = 3, .travel_divisor = 1000000, .signs = {}};
}

RaidRules full_rides() {
    return RaidRules{.loot_divisor = 1, .loot_share = 10, .travel_divisor = 1000000, .signs = {}};
}

}

TEST_CASE("a raid on an empty house is marked undefended") {
    const TestPaths paths{"raid-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":2000,"bob":0,"carol":1000},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":500,"carol":1000}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    CHECK(raid_start(storage, 0, "alice", "carol", 0, full_rides()).status == RaidStatus::started);
    CHECK(raid_start(storage, 0, "bob", "alice", 0, full_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> arrivals = raid_due(storage, 5, full_rides());
    REQUIRE(arrivals.size() == 2);
    CHECK(arrivals[1].raider == "bob");
    CHECK(arrivals[1].undefended);
    /* A tenth of her 2000 palle: nobody at home to stand in the way. */
    CHECK(arrivals[1].loot == 200);
}

TEST_CASE("the balloon guards only where its owner is, and follows him home") {
    const TestPaths paths{"raid-balloon-holder-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},)"
             << R"("scores":{"alice":2000,"bob":0},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"ids":{"alice":0,"bob":5000}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    /* She is in @TheConquister37 with it, so her house is empty. */
    CHECK(raid_start(storage, 0, "bob", "alice", 0, full_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> first = raid_due(storage, 5, full_rides());
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == RaidEvent::Kind::stolen);
    CHECK(first[0].undefended);
    CHECK_FALSE(first[0].balloon_held);
    CHECK_FALSE(first[0].balloon_popped);
    CHECK(read_json(paths.conquister).at("balloons").at("alice") == 3);

    static_cast<void>(raid_due(storage, 10, full_rides()));
    CHECK(raid_start(storage, 1, "alice", "alice", 11, full_rides()).status == RaidStatus::left_place);
    CHECK(read_json(paths.conquister).at("balloons").at("alice") == 3);
    /* Home with the same balloon, three attempts already behind it: the fourth pops it for sure. */
    CHECK(raid_start(storage, 0, "bob", "alice", 12, full_rides()).status == RaidStatus::started);
    const std::vector<RaidEvent> second = raid_due(storage, 17, full_rides());
    REQUIRE(second.size() == 1);
    CHECK_FALSE(second[0].undefended);
    CHECK(second[0].kind == RaidEvent::Kind::stolen);
    CHECK(second[0].balloon_popped);
    CHECK(second[0].loot > 0);
    CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("alice"));
}

TEST_CASE("raid resistance follows the victim across attackers, persists, and recovers") {
    const TestPaths paths{"raid-resistance-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        /* Her boost keeps her balloon out of the way, so every raid gets through. */
        file << R"({"current":null,"scores":{"alice":1000000,"bob":0,"carol":0},)"
             << R"("ids":{"alice":0,"bob":5000,"carol":4000},"boosts":{"alice":3}})";
    }
    RaidRules rules = full_rides();
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
        /* Her boost keeps her balloon out of the way, so the raid gets through. */
        file << R"({"current":null,"scores":{"alice":1000,"bob":40},"quotes_added":{},)"
             << R"("boosts":{"alice":3}})";
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

TEST_CASE("palle brought back to the place leave the game") {
    const TestPaths paths{"palle-burn-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    CHECK(palle_burn(storage, "alice", 0).status == BurnStatus::invalid_amount);
    CHECK(palle_burn(storage, "alice", -5).status == BurnStatus::invalid_amount);
    const BurnResult refused = palle_burn(storage, "alice", 1001);
    CHECK(refused.status == BurnStatus::insufficient_score);
    CHECK(refused.score == 1000);
    CHECK(conquister_user(storage, "alice")->score == 1000);

    const BurnResult burned = palle_burn(storage, "alice", 400);
    CHECK(burned.status == BurnStatus::burned);
    CHECK(burned.amount == 400);
    CHECK(burned.score == 600);
    CHECK(conquister_user(storage, "alice")->score == 600);
    /* Nobody else grew by what was destroyed. */
    CHECK(wealth_now(storage).total == 600);
}

TEST_CASE("palle taken along change hands on arrival and come home on a turnaround") {
    const TestPaths paths{"raid-gift-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":500},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    CHECK(raid_start(storage, 7, "bob", "alice", 0, quick_rides(), RaidTargetKind::any, 600).status ==
          RaidStatus::insufficient_score);
    CHECK(conquister_user(storage, "bob")->score == 500);

    const RaidResult start = raid_start(storage, 7, "bob", "alice", 0, quick_rides(), RaidTargetKind::any, 200);
    REQUIRE(start.status == RaidStatus::started);
    /* The palle leave with him, so nothing on the road can be taken from them. */
    CHECK(start.score == 300);
    CHECK(conquister_user(storage, "bob")->score == 300);

    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::delivered);
    CHECK(arrival[0].gift == 200);
    CHECK(arrival[0].loot == 0);
    /* Whoever came to give takes nothing away. */
    CHECK(conquister_user(storage, "alice")->score == 1200);

    const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
    REQUIRE(home.size() == 1);
    CHECK(home[0].kind == RaidEvent::Kind::returned);
    CHECK(home[0].gift == 0);
    CHECK(home[0].loot == 0);
    CHECK(conquister_user(storage, "bob")->score == 300);

    /* Turning back halfway brings the palle home: nobody received them. */
    REQUIRE(raid_start(storage, 7, "bob", "alice", 11, quick_rides(), RaidTargetKind::any, 100).status ==
            RaidStatus::started);
    CHECK(conquister_user(storage, "bob")->score == 200);
    const RaidResult back = raid_start(storage, 7, "bob", "bob", 13, quick_rides());
    REQUIRE(back.status == RaidStatus::coming_home);
    CHECK(conquister_user(storage, "alice")->score == 1200);
    const std::vector<RaidEvent> returned = raid_due(storage, 13 + back.seconds, quick_rides());
    REQUIRE(returned.size() == 1);
    CHECK(returned[0].kind == RaidEvent::Kind::returned);
    CHECK(returned[0].gift == 100);
    CHECK(conquister_user(storage, "bob")->score == 300);
    CHECK(conquister_user(storage, "alice")->score == 1200);
}

TEST_CASE("a balloon at home holds off raids until it pops") {
    const TestPaths paths{"raid-balloon-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":500},"quotes_added":{},)"
             << R"("balloons":{"alice":0}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    /* Kept across a restart: a balloon at home is no longer a leftover. */
    CHECK(read_json(paths.conquister).at("balloons").at("alice") == 0);

    int raids = 0;
    RaidEvent arrival;
    do {
        const std::int64_t now = std::int64_t{20} * raids;
        REQUIRE(raid_start(storage, 0, "bob", "alice", now, quick_rides()).status == RaidStatus::started);
        const std::vector<RaidEvent> arrived = raid_due(storage, now + 5, quick_rides());
        REQUIRE(arrived.size() == 1);
        arrival = arrived[0];
        ++raids;
        if (arrival.balloon_held) {
            /* Nothing taken, nothing to build resistance on, and the next raid has better odds. */
            CHECK(arrival.loot == 0);
            CHECK(arrival.next_chance == 25 * (raids + 1));
            CHECK(conquister_user(storage, "alice")->score == 1000);
            CHECK_FALSE(read_json(paths.conquister).at("raid_resistance_levels").contains("alice"));
        }
        const std::vector<RaidEvent> home = raid_due(storage, now + 10, quick_rides());
        REQUIRE(home.size() == 1);
        CHECK(home[0].loot == arrival.loot);
    } while (arrival.balloon_held && raids < 8);

    CHECK(raids <= 4);
    CHECK(arrival.balloon_popped);
    CHECK(arrival.loot > 0);
    CHECK(conquister_user(storage, "alice")->score == 1000 - arrival.loot);
    CHECK_FALSE(read_json(paths.conquister).at("balloons").contains("alice"));
}

TEST_CASE("an empty house has no defences") {
    const TestPaths paths{"raid-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":0,)"
             << R"("carol":800},"quotes_added":{},"balloons":{"alice":0}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* Her balloon stays with her: out on the road, the house she left has nothing in it. */
    static_cast<void>(raid_start(storage, 0, "alice", "carol", 0, quick_rides()));
    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));

    const std::vector<RaidEvent> arrivals = raid_due(storage, 5, quick_rides());
    REQUIRE(arrivals.size() == 2);
    for (const RaidEvent &event : arrivals) {
        CHECK(event.kind == RaidEvent::Kind::stolen);
        if (event.raider == "bob") {
            CHECK(event.undefended);
            CHECK_FALSE(event.balloon_held);
            CHECK_FALSE(event.balloon_popped);
        }
    }
    CHECK(read_json(paths.conquister).at("balloons").at("alice") == 0);
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
        storage.transaction([](StorageSession &session) {
            session.state().balloons["alice"] = 3;
            return 0;
        });
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
    const RaidRules slow{.loot_divisor = 50, .loot_share = 0, .travel_divisor = 1, .signs = {}};

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

TEST_CASE("investments earn zodiac interest, remain separate from score, and survive a restart") {
    const TestPaths paths{"investment-service-test"};
    const std::int64_t first_day = zodiac::next_day_start(0);
    const std::int64_t second_day = zodiac::next_day_start(first_day);
    const std::int64_t third_day = zodiac::next_day_start(second_day);
    {
        Storage storage{paths.conquister, paths.quotes};
        const std::string alice = player_seen(storage, 1, "Alice");
        const std::string bob = player_seen(storage, 2, "Bob");
        storage.transaction([&](StorageSession &session) {
            session.state().scores[alice] = 3000;
            session.state().investment_magnitudes[std::to_string(first_day)] = 100;
            session.state().investment_magnitudes[std::to_string(second_day)] = 100;
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
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, first_day).status ==
              InvestmentStatus::deposited);
        CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 2000);
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, second_day).status ==
              InvestmentStatus::deposited);
        CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000);
        CHECK(investment_withdraw(storage, bob, "Alice", RaidTargetKind::telegram, second_day).status ==
              InvestmentStatus::not_self);
    }
    {
        Storage storage{paths.conquister, paths.quotes};
        const InvestmentResult payout = investment_withdraw(storage, "tg:1", "Alice", RaidTargetKind::telegram,
                                                              third_day);
        const int first_rate = bank_rate("Alice", first_day, 100);
        const int second_rate = bank_rate("Alice", second_day, 100);
        const std::int64_t first_factor = 1 + first_rate / 100;
        const std::int64_t second_factor = 1 + second_rate / 100;
        const std::int64_t expected = 1000 * first_factor * second_factor + 1000 * second_factor;
        CHECK(payout.status == InvestmentStatus::withdrawn);
        CHECK(payout.amount == expected);
        CHECK(payout.interest == expected - 2000);
        CHECK(payout.score == expected + 1000);
        CHECK(investment_withdraw(storage, "tg:1", "Alice", RaidTargetKind::telegram, third_day).status ==
              InvestmentStatus::no_investment);
        CHECK(read_json(paths.conquister).at("investments").empty());
    }
}

TEST_CASE("zodiac bank returns double, preserve, or wipe out deposits") {
    const TestPaths paths{"zodiac-investment-test"};
    const std::int64_t first_day = zodiac::next_day_start(0);
    const std::int64_t second_day = zodiac::next_day_start(first_day);
    const std::int64_t third_day = zodiac::next_day_start(second_day);
    const std::int64_t fourth_day = zodiac::next_day_start(third_day);
    const std::int64_t fifth_day = zodiac::next_day_start(fourth_day);
    const zodiac::Element house = zodiac::element_of_day(first_day);
    const std::vector<zodiac::Override> signs{{"Alice", std::string{matching_sign(house)}},
                                               {"Bob", std::string{opposing_sign(house)}},
                                               {"Carol", std::string{matching_sign(house)}},
                                               {"Dave", std::string{neutral_sign(house)}},
                                               {"Eve", std::string{opposing_sign(house)}}};
    Storage storage{paths.conquister, paths.quotes};
    const std::string alice = player_seen(storage, 1, "Alice");
    const std::string bob = player_seen(storage, 2, "Bob");
    const std::string carol = player_seen(storage, 3, "Carol");
    const std::string dave = player_seen(storage, 4, "Dave");
    const std::string eve = player_seen(storage, 5, "Eve");
    storage.transaction([&](StorageSession &session) {
        session.state().scores[alice] = 1000;
        session.state().scores[bob] = 1000;
        session.state().scores[carol] = 1000;
        session.state().scores[dave] = 1000;
        session.state().scores[eve] = 1000;
        for (const std::int64_t day : {first_day, second_day, third_day, fourth_day}) {
            session.state().investment_magnitudes[std::to_string(day)] = 100;
        }
        return 0;
    });
    CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, first_day, signs)
              .zodiac_percent == 125);
    CHECK(investment_deposit(storage, bob, "Bob", RaidTargetKind::telegram, 1000, first_day, signs)
              .daily_rate == -100);
    CHECK(investment_deposit(storage, carol, "Carol", RaidTargetKind::telegram, 1000, first_day, signs)
              .daily_rate == 100);
    CHECK(investment_deposit(storage, dave, "Dave", RaidTargetKind::telegram, 1000, first_day, signs)
              .daily_rate == 0);
    CHECK(investment_deposit(storage, eve, "Eve", RaidTargetKind::telegram, 1000, first_day, signs)
              .daily_rate == -100);
    CHECK(investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram, second_day, signs).amount == 2000);
    CHECK(investment_withdraw(storage, bob, "Bob", RaidTargetKind::telegram, second_day, signs).amount == 0);
    CHECK(investment_withdraw(storage, dave, "Dave", RaidTargetKind::telegram, second_day, signs).amount == 1000);
    CHECK(investment_withdraw(storage, eve, "Eve", RaidTargetKind::telegram, first_day + 43200, signs).amount == 500);
    CHECK(investment_withdraw(storage, carol, "Carol", RaidTargetKind::telegram, fifth_day, signs).amount == 0);
}

TEST_CASE("an intermediate bank magnitude is shared and survives a restart") {
    const TestPaths paths{"intermediate-investment-magnitude-test"};
    const std::int64_t first_day = zodiac::next_day_start(0);
    const std::int64_t second_day = zodiac::next_day_start(first_day);
    const zodiac::Element house = zodiac::element_of_day(first_day);
    const std::vector<zodiac::Override> signs{{"Alice", std::string{matching_sign(house)}},
                                               {"Bob", std::string{opposing_sign(house)}},
                                               {"Carol", std::string{neutral_sign(house)}}};
    {
        Storage storage{paths.conquister, paths.quotes};
        const std::string alice = player_seen(storage, 1, "Alice");
        const std::string bob = player_seen(storage, 2, "Bob");
        const std::string carol = player_seen(storage, 3, "Carol");
        storage.transaction([&](StorageSession &session) {
            session.state().scores[alice] = 1000;
            session.state().scores[bob] = 1000;
            session.state().scores[carol] = 1000;
            session.state().investment_magnitudes[std::to_string(first_day)] = 37;
            return 0;
        });
        CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000, first_day, signs)
                  .daily_rate == 37);
        CHECK(investment_deposit(storage, bob, "Bob", RaidTargetKind::telegram, 1000, first_day, signs)
                  .daily_rate == -37);
        CHECK(investment_deposit(storage, carol, "Carol", RaidTargetKind::telegram, 1000, first_day, signs)
                  .daily_rate == 0);
    }
    CHECK(read_json(paths.conquister).at("investment_magnitudes").at(std::to_string(first_day)) == 37);
    Storage reopened{paths.conquister, paths.quotes};
    CHECK(investment_withdraw(reopened, "tg:1", "Alice", RaidTargetKind::telegram, second_day, signs).amount == 1370);
    CHECK(investment_withdraw(reopened, "tg:2", "Bob", RaidTargetKind::telegram, second_day, signs).amount == 630);
    CHECK(investment_withdraw(reopened, "tg:3", "Carol", RaidTargetKind::telegram, second_day, signs).amount == 1000);
}

TEST_CASE("a newly drawn bank magnitude is saved for the whole local day") {
    const TestPaths paths{"drawn-investment-magnitude-test"};
    const std::int64_t first_day = zodiac::next_day_start(0);
    const zodiac::Element house = zodiac::element_of_day(first_day);
    const std::vector<zodiac::Override> signs{{"Alice", std::string{matching_sign(house)}},
                                               {"Bob", std::string{opposing_sign(house)}}};
    int chosen = -1;
    {
        Storage storage{paths.conquister, paths.quotes};
        const std::string alice = player_seen(storage, 1, "Alice");
        const std::string bob = player_seen(storage, 2, "Bob");
        storage.transaction([&](StorageSession &session) {
            session.state().scores[alice] = 1000;
            session.state().scores[bob] = 1000;
            return 0;
        });
        chosen = investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1, first_day, signs).daily_rate;
        CHECK(chosen >= 0);
        CHECK(chosen <= 100);
        CHECK(investment_deposit(storage, bob, "Bob", RaidTargetKind::telegram, 1, first_day, signs).daily_rate ==
              -chosen);
    }
    CHECK(read_json(paths.conquister).at("investment_magnitudes").at(std::to_string(first_day)) == chosen);
    Storage reopened{paths.conquister, paths.quotes};
    CHECK(investment_deposit(reopened, "tg:1", "Alice", RaidTargetKind::telegram, 1, first_day, signs).daily_rate ==
          chosen);
}

TEST_CASE("daily investment returns accrue at one-second resolution") {
    const TestPaths paths{"investment-per-second-test"};
    Storage storage{paths.conquister, paths.quotes};
    const std::string alice = player_seen(storage, 1, "Alice");
    const std::vector<zodiac::Override> signs{{"Alice", std::string{matching_sign(zodiac::element_of_day(0))}}};
    storage.transaction([&](StorageSession &session) {
        session.state().scores[alice] = 1000000;
        session.state().investment_magnitudes[std::to_string(zodiac::day_start(0))] = 100;
        return 0;
    });
    CHECK(investment_deposit(storage, alice, "Alice", RaidTargetKind::telegram, 1000000, 0, signs).status ==
          InvestmentStatus::deposited);
    const InvestmentResult payout = investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram, 1, signs);
    CHECK(payout.status == InvestmentStatus::withdrawn);
    CHECK(payout.amount == 1000011);
    CHECK(payout.interest == 11);
}

TEST_CASE("old compound investments keep their accrued return through the changeover") {
    const TestPaths paths{"legacy-investment-rate-test"};
    const std::int64_t before = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << Json{{"current", nullptr}, {"scores", Json{{"tg:1", 0}}},
                     {"telegram_names", Json{{"alice", "tg:1"}}},
                     {"investments", Json::array({Json{{"player", "tg:1"}, {"amount", 1000},
                                                        {"since", before - 86400}}})}}.dump();
    }
    Storage storage{paths.conquister, paths.quotes};
    const Json saved = read_json(paths.conquister);
    const std::int64_t cutover = saved.at("investments").at(0).at("fixed_until").get<std::int64_t>();
    CHECK(cutover >= before);
    const InvestmentResult payout = investment_withdraw(storage, "tg:1", "Alice", RaidTargetKind::telegram,
                                                         cutover);
    CHECK(payout.status == InvestmentStatus::withdrawn);
    CHECK(payout.amount == 1030);
}

TEST_CASE("migrated investments switch to zodiac returns after the changeover") {
    const TestPaths paths{"legacy-investment-cutover-test"};
    const std::int64_t before = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << Json{{"current", nullptr}, {"scores", Json{{"tg:1", 0}}},
                     {"telegram_names", Json{{"alice", "tg:1"}}},
                     {"display_names", Json{{"tg:1", "Alice"}}},
                     {"investments", Json::array({Json{{"player", "tg:1"}, {"amount", 1000000000},
                                                        {"since", before - 86400}}})}}.dump();
    }
    Storage storage{paths.conquister, paths.quotes};
    const std::int64_t cutover = read_json(paths.conquister).at("investments").at(0)
                                     .at("fixed_until").get<std::int64_t>();
    const std::int64_t elapsed = std::min<std::int64_t>(60, zodiac::next_day_start(cutover) - cutover);
    const std::vector<zodiac::Override> signs{{"Alice", std::string{matching_sign(zodiac::element_of_day(cutover))}}};
    storage.transaction([&](StorageSession &session) {
        session.state().investment_magnitudes[std::to_string(zodiac::day_start(cutover))] = 100;
        return 0;
    });
    const InvestmentResult payout = investment_withdraw(storage, "tg:1", "Alice", RaidTargetKind::telegram,
                                                         cutover + elapsed, signs);
    const long double old_balance = 1000000000.0L * std::pow(1.03L,
        static_cast<long double>(cutover - (before - 86400)) / 86400.0L);
    const long double expected = old_balance * (1.0L + static_cast<long double>(elapsed) /
        static_cast<long double>(zodiac::next_day_start(cutover) - zodiac::day_start(cutover)));
    CHECK(payout.status == InvestmentStatus::withdrawn);
    CHECK(payout.amount == static_cast<std::int64_t>(std::floor(std::nextafter(
        expected, std::numeric_limits<long double>::infinity()))));
}

TEST_CASE("investment withdrawal and deposit require the player to be home") {
    const TestPaths paths{"investment-location-test"};
    Storage storage{paths.conquister, paths.quotes};
    const std::string alice = player_seen(storage, 1, "Alice");
    CHECK_FALSE(player_seen(storage, 2, "Bob").empty());
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
                          .signs = {}};
    const RaidResult trip = raid_start(storage, 2, bob, "Alice", 1, rules, RaidTargetKind::telegram);
    REQUIRE(trip.status == RaidStatus::started);
    const std::vector<RaidEvent> arrival = raid_due(storage, 1 + trip.seconds, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].loot <= 1000);
    CHECK(conquister_user(storage, "Alice", RaidTargetKind::telegram)->score == 1000 - arrival[0].loot);
    const InvestmentResult payout = investment_withdraw(storage, alice, "Alice", RaidTargetKind::telegram,
                                                         1 + trip.seconds);
    CHECK(payout.status == InvestmentStatus::withdrawn);
    CHECK(payout.amount > 0);
    CHECK(payout.score == 1000 - arrival[0].loot + payout.amount);
}
