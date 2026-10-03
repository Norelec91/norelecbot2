#include "test_paths.hpp"

#include "game.hpp"
#include "storage.hpp"
#include "position.hpp"
#include "zodiac.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <chrono>

using namespace norelecbot;

namespace {

/* Seconds held, times the ⚡ in percent, times what the house of the day was worth to the holder. */
std::int64_t earnings(std::string_view holder, std::int64_t seconds, std::int64_t now, std::int64_t lightning = 100) {
    return seconds * lightning / 100 * zodiac::percent_for(holder, now) / 100;
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
    CHECK(entered.entered_lightning == 0);
    /* Everybody starts with a 🎈: a fresh one leaves no trace on file until something hits it. */
    balloons_hand_out(storage, 10);
    CHECK(furniture_all(storage).at("alice") == "🎈");
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
    /* Popped and kicked out, her 🎈 is fresh again and still on her name. */
    CHECK(read_json(paths.conquister).at("balloons").empty());
    CHECK(furniture_all(storage).at("alice") == "🎈");

    /* Worn at the place, the balloon goes home with him just as worn: it is the same one. */
    storage.transaction([](StorageSession &session) {
        session.state().balloons["bob"] = 2;
        return 0;
    });
    CHECK(raid_start(storage, 2, "bob", "bob", 9000, RaidRules{}).status == RaidStatus::left_place);
    CHECK(read_json(paths.conquister).at("balloons").at("bob") == 2);
    const ClaimResult again = conquister_claim(storage, 1, "alice", 9001);
    CHECK(again.status == ClaimStatus::taken);
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

    const ClaimResult blocked = conquister_claim(storage, 2, "bob", 700, ClaimRules{.cooldown_seconds = 300, .signs = {}});
    CHECK(blocked.status == ClaimStatus::cooldown);
    CHECK(blocked.penalty_seconds == 300);

    const ClaimResult others = conquister_claim(storage, 3, "carol", 700, ClaimRules{.cooldown_seconds = 300, .signs = {}});
    CHECK(others.status == ClaimStatus::taken);
    CHECK(raid_start(storage, 3, "carol", "carol", 900, RaidRules{}).status == RaidStatus::left_place);

    const ClaimResult expired = conquister_claim(storage, 2, "bob", 1000, ClaimRules{.cooldown_seconds = 300, .signs = {}});
    CHECK(expired.status == ClaimStatus::taken);
}

TEST_CASE("a failed balloon attempt hands out the penalty") {
    const TestPaths paths{"cooldown-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(conquister_claim(storage, 1, "alice", 0).status == ClaimStatus::taken);
    balloons_hand_out(storage, 10);

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 3000, ClaimRules{.cooldown_seconds = 300, .signs = {}});
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.penalty_seconds == 300);
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100, ClaimRules{.cooldown_seconds = 300, .signs = {}});
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
             << R"("quotes_added":{},"balloons":{"alice":3},"furniture":{"alice":"🍕🎈"}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 10);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
    /* Popped, the 🎈 stays on her name. It is having it that defends: once she burns it, the next one
       walks in. */
    CHECK(furniture_all(storage).at("alice") == "🍕🎈");
    CHECK(furniture_burn(storage, "alice", "🎈", 15).status == FurnitureBurnStatus::burned);
    CHECK(conquister_claim(storage, 1, "alice", 20).status == ClaimStatus::taken);
    const ClaimResult unguarded = conquister_claim(storage, 2, "bob", 30);
    CHECK(unguarded.status == ClaimStatus::taken);
    CHECK_FALSE(unguarded.balloon_popped);
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
    balloons_hand_out(storage, 10);
    storage.transaction([](StorageSession &session) {
        session.state().balloons["alice"] = 3;
        return 0;
    });
    const ClaimResult attack = conquister_claim(storage, 2, "bob", now);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
}

TEST_CASE("every ⚡ on the name as a player comes in adds its share to that hold") {
    const TestPaths paths{"lightning-test"};
    {
        /* Alice has ⚡; both balloons already took three attempts, so every claim gets in. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":0},"quotes_added":{},)"
             << R"("furniture":{"alice":"🍕⚡"},"balloons":{"alice":3,"bob":3}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const ClaimRules rules{.cooldown_seconds = 0, .signs = {}, .lightning = 50};
    const auto worn_out = [&storage] {
        storage.transaction([](StorageSession &session) {
            session.state().balloons["alice"] = 3;
            session.state().balloons["bob"] = 3;
            return 0;
        });
    };

    const ClaimResult entered = conquister_claim(storage, 1, "alice", 0, rules);
    CHECK(entered.entered_lightning == 150);
    /* Burning the ⚡ partway lowers the hold from then on: what it made so far is kept as it was. */
    CHECK(furniture_burn(storage, "alice", "⚡", 400).status == FurnitureBurnStatus::burned);
    CHECK(player_profile_of(storage, "alice", 400).lightning_percent == 0);
    const ClaimResult kicked = conquister_claim(storage, 2, "bob", 1000, rules);
    CHECK(kicked.previous_username == "alice");
    CHECK(kicked.lightning == 0);
    CHECK(kicked.earned == earnings("alice", 400, 400, 150) + earnings("alice", 600, 1000));
    CHECK(kicked.entered_lightning == 0);

    /* Coming in without one, a ⚡ that arrives later does not count either. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "⚡";
        return 0;
    });
    worn_out();
    const ClaimResult plain = conquister_claim(storage, 1, "alice", 1500, rules);
    CHECK(plain.lightning == 0);
    CHECK(plain.earned == earnings("bob", 500, 1500));
    CHECK(plain.entered_lightning == 0);

    /* Three ⚡ add up to x2.5, and the balloon stays: the ⚡ only multiplies. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "⚡⚡⚡";
        return 0;
    });
    worn_out();
    const ClaimResult bolt = conquister_claim(storage, 2, "bob", 2000, rules);
    CHECK(bolt.entered_lightning == 250);
    const RaidResult left = raid_start(storage, 2, "bob", "bob", 2100, RaidRules{});
    CHECK(left.status == RaidStatus::left_place);
    CHECK(left.lightning == 250);
    CHECK(left.earned == earnings("bob", 100, 2100, 250));
}

TEST_CASE("a 🦞 comes in as what the kicked holder has in the same slot, and leaves as a 🦞") {
    const TestPaths paths{"lobster-test"};
    {
        /* Both balloons already took three attempts, so every claim gets in. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":0,"carol":0},"quotes_added":{},)"
             << R"("furniture":{"alice":"⚡[]🎈🦞","bob":"🦞🦞🍕🦞"},"balloons":{"alice":3,"bob":3,"carol":3}})";
    }
    const ClaimRules rules{.cooldown_seconds = 0, .signs = {}, .lightning = 50};
    {
        Storage storage{paths.conquister, paths.quotes};
        /* Into an empty place nothing is copied. */
        CHECK(conquister_claim(storage, 1, "alice", 0, rules).lobsters_became.empty());

        /* Slot 1 copies the ⚡ and counts as one; slot 2 finds nothing, slot 4 finds another 🦞. */
        const ClaimResult entered = conquister_claim(storage, 2, "bob", 100, rules);
        CHECK(entered.lobsters_became == std::vector<std::string>{"⚡"});
        CHECK(entered.entered_lightning == 150);
        CHECK(player_profile_of(storage, "bob", 100).furniture == "⚡🦞🍕🦞");
        CHECK(furniture_all(storage).at("bob") == "⚡🦞🍕🦞");
        CHECK(furniture_all(storage).at("alice") == "⚡[]🎈🦞");
    }
    /* The copy survives a restart, and what is saved on the name is still the 🦞. */
    const Json saved = read_json(paths.conquister);
    CHECK(saved.at("furniture").at("bob") == "🦞🦞🍕🦞");
    CHECK(saved.at("current").at("lobsters").at("0") == "⚡");
    Storage storage{paths.conquister, paths.quotes};
    CHECK(player_profile_of(storage, "bob", 200).furniture == "⚡🦞🍕🦞");

    /* The ⚡ it became is not his to burn; burning that 🦞 takes what it had become with it. */
    CHECK(furniture_burn(storage, "bob", "⚡").status == FurnitureBurnStatus::not_owned);
    CHECK(furniture_burn(storage, "bob", "🦞").shown == "[]🦞🍕🦞");
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🦞🦞🍕🦞";
        return 0;
    });

    /* Kicked out, he is back to his 🦞. The copied ⚡ went with the 🦞 he burnt, and its share with it. */
    const ClaimResult kicked = conquister_claim(storage, 3, "carol", 300, rules);
    CHECK(kicked.lightning == 0);
    CHECK(kicked.lobsters_became.empty());
    CHECK(player_profile_of(storage, "bob", 300).furniture == "🦞🦞🍕🦞");
    CHECK(furniture_all(storage).at("bob") == "🦞🦞🍕🦞");
}

TEST_CASE("bought boosts leave old saves without a refund") {
    const TestPaths paths{"boost-retired-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":2000},"quotes_added":{},"boosts":{"alice":3}})";
    }
    const Storage storage{paths.conquister, paths.quotes};
    const Json saved = read_json(paths.conquister);
    CHECK_FALSE(saved.contains("boosts"));
    CHECK(saved.at("scores").at("alice") == 2000);
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

namespace {

/* Far enough apart that every ride is the shortest one, so the tests do not depend on where ids land. */
RaidRules quick_rides() {
    return RaidRules{.loot_divisor = 50, .travel_divisor = 1000000, .signs = {}};
}

RaidRules full_rides() {
    return RaidRules{.loot_divisor = 1, .travel_divisor = 1000000, .signs = {}};
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
    /* The whole road, with nobody at home to stand in the way. */
    CHECK(arrivals[1].loot == arrivals[1].distance * arrivals[1].raider_percent / arrivals[1].target_percent);
}

TEST_CASE("the balloon guards only where its owner is, and follows him home") {
    const TestPaths paths{"raid-balloon-holder-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},)"
             << R"("scores":{"alice":2000,"bob":0},"quotes_added":{},)"
             << R"("balloons":{"alice":3},"ids":{"alice":0,"bob":5000},"furniture":{"alice":"🎈"}})";
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

TEST_CASE("every raid that gets through takes the whole road, however many came before") {
    const TestPaths paths{"raid-no-resistance-test"};
    {
        /* The resistance an older version saved is dropped on the first load and never softens a raid
           again. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000000,"bob":0,"carol":0},)"
             << R"("ids":{"alice":0,"bob":5000,"carol":4000},)"
             << R"("raid_resistance_levels":{"alice":3},"raid_resistance_since":{"alice":0}})";
    }
    const RaidRules rules = full_rides();
    const auto potential = [](const RaidEvent &event) {
        return event.distance * event.raider_percent / event.target_percent;
    };
    Storage storage{paths.conquister, paths.quotes};
    CHECK_FALSE(read_json(paths.conquister).contains("raid_resistance_levels"));
    CHECK_FALSE(read_json(paths.conquister).contains("raid_resistance_since"));
    for (const auto &[raider, start] : {std::pair{"bob", 0}, std::pair{"carol", 11}, std::pair{"bob", 22}}) {
        /* Her balloon already took three attempts each time, so every raid gets through. */
        storage.transaction([](StorageSession &session) {
            session.state().balloons["alice"] = 3;
            return 0;
        });
        REQUIRE(raid_start(storage, 0, raider, "alice", start, rules).status == RaidStatus::started);
        const auto arrival = raid_due(storage, start + 5, rules);
        REQUIRE(arrival.size() == 1);
        CHECK(arrival[0].loot == potential(arrival[0]));
        static_cast<void>(raid_due(storage, start + 10, rules));
    }
}

TEST_CASE("a raid takes what the road allows, up to all the target has, and carries it home") {
    const TestPaths paths{"raid-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        /* Her balloon already took three attempts, so the raid gets through. */
        file << R"({"current":null,"scores":{"alice":1000,"bob":40},"quotes_added":{},)"
             << R"("balloons":{"alice":3}})";
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
    /* A palla for every unit of road, never more than the target owns. */
    CHECK(arrival[0].distance > 0);
    const std::int64_t carried = (arrival[0].distance / 50) *
        zodiac::percent_for("bob", 5) / zodiac::percent_for("alice", 5);
    const std::int64_t loot = std::min(carried, std::int64_t{1000});
    CHECK(arrival[0].loot == loot);
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
    CHECK(read_json(paths.conquister).at("scores").size() == 1);
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
             << R"("balloons":{"alice":0},"furniture":{"alice":"🎈"}})";
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
            /* Nothing taken, and the next raid has better odds. */
            CHECK(arrival.loot == 0);
            CHECK(arrival.next_chance == 25 * (raids + 1));
            CHECK(conquister_user(storage, "alice")->score == 1000);
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


TEST_CASE("furniture hangs in numbered slots, leaves holes and can be overwritten") {
    const TestPaths paths{"furniture-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":25000,"bob":100},"quotes_added":{},)"
             << R"("furniture":{"carol":"🎲🧀"}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* An older save, emoji one after the other, reads as the first slots. */
    CHECK(furniture_slots("🎲🧀") == std::vector<std::string>{"🎲", "🧀"});
    CHECK(furniture_slots("🎈[]🍕[][]🐟") == std::vector<std::string>{"🎈", "", "🍕", "", "", "🐟"});
    CHECK(furniture_stored({"🎈", "", "🍕", "", ""}) == "🎈[]🍕");

    /* No slot named: the first empty one. */
    const FurnitureResult first = furniture_buy(storage, "alice", "🎈", 0, 1000, 10, 0);
    CHECK(first.status == FurnitureStatus::bought);
    CHECK(first.position == 1);
    CHECK(first.shown == "🎈");
    CHECK(first.charged == 1000);
    CHECK(conquister_user(storage, "alice")->score == 24000);

    /* A slot further on leaves a hole, shown as [] only between emoji. */
    const FurnitureResult third = furniture_buy(storage, "alice", "🍕", 3, 1000, 10, 0);
    CHECK(third.status == FurnitureStatus::bought);
    CHECK(third.shown == "🎈[]🍕");
    CHECK(third.replaced.empty());

    /* The hole is the first empty slot now. */
    CHECK(furniture_buy(storage, "alice", "🐟", 0, 1000, 10, 0).shown == "🎈🐟🍕");

    /* Overwriting pays the full price and says what was there. */
    const FurnitureResult swapped = furniture_buy(storage, "alice", "🚀", 3, 1000, 10, 0);
    CHECK(swapped.status == FurnitureStatus::bought);
    CHECK(swapped.replaced == "🍕");
    CHECK(swapped.shown == "🎈🐟🚀");
    CHECK(conquister_user(storage, "alice")->score == 21000);

    /* The same emoji in the same slot, or a slot that does not exist, costs nothing. */
    const FurnitureResult same = furniture_buy(storage, "alice", "🚀", 3, 1000, 10, 0);
    CHECK(same.status == FurnitureStatus::already_there);
    CHECK(furniture_buy(storage, "alice", "🎺", 11, 1000, 10, 0).status == FurnitureStatus::invalid_position);
    CHECK(furniture_buy(storage, "alice", "🎺", -1, 1000, 10, 0).status == FurnitureStatus::invalid_position);
    CHECK(conquister_user(storage, "alice")->score == 21000);

    /* Every slot full and none named: refused without a charge. */
    for (std::int64_t slot = 4; slot <= 10; ++slot) {
        REQUIRE(furniture_buy(storage, "alice", "🌊", slot, 0, 10, 0).status == FurnitureStatus::bought);
    }
    CHECK(furniture_buy(storage, "alice", "🎺", 0, 1000, 10, 0).status == FurnitureStatus::full);

    /* With no palle nothing is bought and nothing is left hanging. */
    const FurnitureResult broke = furniture_buy(storage, "bob", "🎈", 0, 1000, 10, 0);
    CHECK(broke.status == FurnitureStatus::insufficient_score);
    CHECK(conquister_user(storage, "bob")->score == 100);
    const Authors hung = furniture_all(storage);
    CHECK(hung.find("bob") == hung.end());

    /* And what was bought survives a trip through the file. */
    Storage again{paths.conquister, paths.quotes};
    const Authors kept = furniture_all(again);
    REQUIRE(kept.find("alice") != kept.end());
    CHECK(kept.find("alice")->second == "🎈🐟🚀🌊🌊🌊🌊🌊🌊🌊");
}

TEST_CASE("an emoji moves to another slot, swapping with what hangs there") {
    const TestPaths paths{"furniture-move-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":10,"bob":10},"quotes_added":{},)"
             << R"("furniture":{"alice":"🍕🎈"}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const FurnitureMoveResult swapped = furniture_move(storage, "alice", 1, 2, 10, 0);
    CHECK(swapped.status == FurnitureMoveStatus::swapped);
    CHECK(swapped.moved == "🍕");
    CHECK(swapped.swapped == "🎈");
    CHECK(swapped.shown == "🎈🍕");

    /* Into an empty slot: the one it left becomes a hole. */
    const FurnitureMoveResult moved = furniture_move(storage, "alice", 1, 4, 10, 0);
    CHECK(moved.status == FurnitureMoveStatus::moved);
    CHECK(moved.swapped.empty());
    CHECK(moved.shown == "[]🍕[]🎈");
    /* And no hole is kept at the end. */
    CHECK(furniture_move(storage, "alice", 4, 1, 10, 0).shown == "🎈🍕");

    /* Nothing to move, a slot that does not exist, or the same slot twice: nothing changes. */
    CHECK(furniture_move(storage, "alice", 3, 1, 10, 0).status == FurnitureMoveStatus::empty_slot);
    CHECK(furniture_move(storage, "alice", 1, 11, 10, 0).status == FurnitureMoveStatus::invalid_position);
    CHECK(furniture_move(storage, "alice", 0, 1, 10, 0).status == FurnitureMoveStatus::invalid_position);
    CHECK(furniture_move(storage, "alice", 2, 2, 10, 0).status == FurnitureMoveStatus::same_position);
    CHECK(furniture_all(storage).at("alice") == "🎈🍕");

    /* From the place it takes her home first. */
    storage.transaction([](StorageSession &session) {
        session.state().current = Holder{.user_id = 0, .username = "alice", .since = 0};
        return 0;
    });
    const FurnitureMoveResult from_place = furniture_move(storage, "alice", 1, 2, 10, 100);
    CHECK(from_place.status == FurnitureMoveStatus::swapped);
    CHECK(from_place.departure.left);
    CHECK_FALSE(conquister_user(storage, "alice")->in_conquister);
    REQUIRE(furniture_move(storage, "alice", 1, 2, 10, 0).status == FurnitureMoveStatus::swapped);

    /* Not from the road. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, quick_rides()).status == RaidStatus::started);
    CHECK(furniture_move(storage, "alice", 1, 2, 10, 0).status == FurnitureMoveStatus::not_home);
    CHECK(furniture_all(storage).at("alice") == "🎈🍕");
}

TEST_CASE("an emoji costs double for every copy already hanging from anybody's name") {
    const TestPaths paths{"furniture-inflation-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":100000,"bob":100000,"carol":100000},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    CHECK(furniture_buy(storage, "alice", "🍕", 0, 1000, 10, 0).charged == 1000);
    const FurnitureResult second = furniture_buy(storage, "bob", "🍕", 0, 1000, 10, 0);
    CHECK(second.copies == 1);
    CHECK(second.charged == 2000);
    /* Copies in his own other slots count as well. */
    const FurnitureResult third = furniture_buy(storage, "bob", "🍕", 2, 1000, 10, 0);
    CHECK(third.copies == 2);
    CHECK(third.charged == 4000);
    CHECK(conquister_user(storage, "bob")->score == 100000 - 2000 - 4000);

    /* Overwritten, a copy stops counting and the price comes down. */
    REQUIRE(furniture_buy(storage, "alice", "🐟", 1, 1000, 10, 0).status == FurnitureStatus::bought);
    CHECK(furniture_buy(storage, "carol", "🍕", 0, 1000, 10, 0).charged == 4000);

    /* A heart is a heart, drawn in colour or not. */
    REQUIRE(furniture_buy(storage, "alice", "❤", 2, 1000, 10, 0).status == FurnitureStatus::bought);
    const FurnitureResult heart = furniture_buy(storage, "carol", "❤️", 2, 1000, 10, 0);
    CHECK(heart.copies == 1);
    CHECK(heart.charged == 2000);

    /* So many copies that the price would wrap around stops at the largest number instead. */
    storage.transaction([](StorageSession &session) {
        std::string lots;
        for (int copy = 0; copy < 70; ++copy) {
            lots += "🐝";
        }
        session.state().furniture["dave"] = lots;
        return 0;
    });
    const FurnitureResult dear = furniture_buy(storage, "alice", "🐝", 3, 1000, 10, 0);
    CHECK(dear.status == FurnitureStatus::insufficient_score);
    CHECK(dear.charged == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("with a lower inflation every copy costs half again, not twice") {
    const TestPaths paths{"furniture-inflation-half-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":100000},"quotes_added":{},)"
             << R"("furniture":{"bob":"⚡⚡⚡⚡"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    /* Four already hang on Bob: 1000 × 1.5⁴, rounded down at every step. */
    const FurnitureResult fifth = furniture_buy(storage, "alice", "⚡", 0, 1000, 10, 0, {}, 50);
    CHECK(fifth.copies == 4);
    CHECK(fifth.charged == 5062);
    CHECK(furniture_buy(storage, "alice", "⚡", 0, 1000, 10, 0, {}, 50).charged == 7593);
}

TEST_CASE("every 🚀 on the name as he leaves makes both legs of the ride faster") {
    const TestPaths paths{"rocket-test"};
    {
        /* A thousand units apart: a thousand seconds each way at one unit a second. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":0,"carol":0},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":1000,"carol":2000},)"
             << R"("furniture":{"bob":"🚀🍕","carol":"🚀🚀🚀"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const RaidRules rules{.loot_divisor = 50, .travel_divisor = 1, .signs = {}, .rocket_percent = 25};

    CHECK(raid_start(storage, 0, "alice", "bob", 0, rules).seconds == 1000);
    /* One 🚀 is +25%: 1000 / 1.25. */
    const RaidResult one = raid_start(storage, 0, "bob", "alice", 0, rules);
    CHECK(one.seconds == 800);
    /* Three add up to +75%: 2000 / 1.75. */
    CHECK(raid_start(storage, 0, "carol", "alice", 0, rules).seconds == 1142);
    const bool both_legs = storage.transaction([](StorageSession &session) {
        const auto &raids = session.state().raids;
        return raids[1].arrive == 800 && raids[1].back == 1600;
    });
    CHECK(both_legs);
}

TEST_CASE("every 🥺 on the target's name when the raider gets there talks him out of 5%") {
    const TestPaths paths{"pleading-test"};
    {
        /* A long road and little to take: the raid could carry off everything. Their balloons already
           took three attempts, so every raid gets through. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"lucy":100,"bob":100},"quotes_added":{},)"
             << R"("ids":{"alice":0,"lucy":90000,"bob":90001},"balloons":{"lucy":3,"bob":3},)"
             << R"("furniture":{"lucy":"🥺🥺🥺🥺🥺🥺🥺🥺🥺🥺","bob":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.loot_divisor = 1;
    rules.pleading_percent = 5;

    REQUIRE(raid_start(storage, 0, "alice", "lucy", 0, rules).status == RaidStatus::started);
    std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    /* Ten of them: half of it stays with her. */
    CHECK(arrival[0].pleaded_percent == 50);
    CHECK(arrival[0].spared == 50);
    CHECK(arrival[0].loot == 50);
    CHECK(conquister_user(storage, "lucy")->score == 50);
    REQUIRE(raid_due(storage, 10, rules).size() == 1);

    REQUIRE(raid_start(storage, 0, "alice", "bob", 10, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 15, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].spared == 0);
    CHECK(arrival[0].loot == 100);
}

TEST_CASE("every emoji with a power is listed once, with its kind") {
    for (std::size_t first = 0; first < powers.size(); ++first) {
        CHECK_FALSE(powers[first].counted.empty());
        CHECK_FALSE(powers[first].effect.empty());
        for (std::size_t second = first + 1; second < powers.size(); ++second) {
            CHECK_FALSE(is_power(powers[first].emoji, powers[second]));
        }
    }
    /* Hung at home, carried along, thrown at somebody. */
    CHECK(power::pleading.kind == PowerKind::home);
    CHECK(power::rocket.kind == PowerKind::carried);
    CHECK(power::bolt.kind == PowerKind::carried);
    CHECK(power::lobster.kind == PowerKind::carried);
    CHECK(power::poo.kind == PowerKind::thrown);
    CHECK(power::bomb.kind == PowerKind::thrown);
    CHECK(power::nuke.kind == PowerKind::thrown);
    CHECK(power_of("☢") == &powers[6]);
    CHECK(power::dog.kind == PowerKind::home);
    CHECK(power::balloon.kind == PowerKind::carried);
    CHECK(power::mailbox.kind == PowerKind::home);
    CHECK(power::ninja.kind == PowerKind::carried);
    CHECK(power::alarm.kind == PowerKind::home);
    CHECK(power::vortex.kind == PowerKind::thrown);
    CHECK(power::pirate.kind == PowerKind::carried);
    CHECK(power::seed.kind == PowerKind::thrown);
    CHECK(power::hen.kind == PowerKind::home);
    CHECK(power::trap.kind == PowerKind::home);
    CHECK(power::ice.kind == PowerKind::thrown);
    CHECK(power::fire.kind == PowerKind::carried);
    CHECK(power::hourglass.kind == PowerKind::carried);
    CHECK(power::dino.kind == PowerKind::home);
    /* The 🕋 and the ⛪ are emoji like any other. */
    CHECK(power_of("🕋") == nullptr);
    CHECK(power_of("⛪") == nullptr);
    /* Whatever the tone of its skin, it is the same emoji. */
    CHECK(is_power("🥷🏿", power::ninja));
    CHECK(is_power("🥷", power::ninja));
    CHECK(power_of("💣") == &powers[5]);
    CHECK(power_of("🍕") == nullptr);
    /* Drawn in colour or not, it is the same emoji. */
    CHECK(is_power("⚡\xEF\xB8\x8F", power::bolt));
    CHECK(is_power("⚡", power::bolt));
    CHECK_FALSE(is_power("🍕", power::bolt));
}

TEST_CASE("a 💣 takes one emoji with a power among those that are where it lands") {
    const TestPaths paths{"bomb-test"};
    {
        /* dave holds the place: the ⚡ and the 🚀 he carries are with him, only his 🥺 is at home. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"dave","since":0},)"
             << R"("scores":{"alice":0,"bob":0,"carol":0,"dave":0},"quotes_added":{},)"
             << R"("furniture":{"alice":"💣💣💣","bob":"🍕🥺🍕","carol":"🍕🍕🍕🍕🍕🍕🍕🍕🍕🍕",)"
             << R"("dave":"⚡🚀🍕🥺"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const auto thrown_at = [&storage](const std::string &target, std::int64_t now) {
        REQUIRE(raid_start(storage, 0, "alice", target, now, quick_rides(), RaidTargetKind::any, 0, "💣").status ==
                RaidStatus::started);
        const std::vector<RaidEvent> arrival = raid_due(storage, now + 5, quick_rides());
        REQUIRE(arrival.size() == 1);
        CHECK(arrival[0].kind == RaidEvent::Kind::delivered);
        CHECK(arrival[0].gift_emoji == "💣");
        const std::vector<RaidEvent> home = raid_due(storage, now + 10, quick_rides());
        REQUIRE(home.size() == 1);
        /* It went off: nothing comes back. */
        CHECK(home[0].gift_emoji.empty());
        return arrival[0];
    };

    /* The 🥺 is the only one with a power: the 🍕 are never touched. */
    const RaidEvent pair = thrown_at("bob", 0);
    CHECK(pair.blown == std::vector<std::string>{"🥺"});
    CHECK(pair.target_emoji == "🍕[]🍕");
    CHECK(furniture_all(storage).at("bob") == "🍕[]🍕");

    /* Nor does it take a 🎈, which no explosion touches. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕🎈🍕";
        session.state().furniture["alice"] = "💣💣💣";
        return 0;
    });
    const RaidEvent spared = thrown_at("bob", 12);
    CHECK(spared.blown.empty());
    CHECK(furniture_all(storage).at("bob") == "🍕🎈🍕");
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕[]🍕";
        return 0;
    });

    /* A full name of plain emoji takes the hit and loses nothing. */
    const RaidEvent nothing = thrown_at("carol", 20);
    CHECK(nothing.blown.empty());
    CHECK(furniture_all(storage).at("carol") == "🍕🍕🍕🍕🍕🍕🍕🍕🍕🍕");

    /* Out of the house, what he carries is safe: only the 🥺 is there to take. */
    const RaidEvent single = thrown_at("dave", 40);
    CHECK(single.blown == std::vector<std::string>{"🥺"});
    CHECK(furniture_all(storage).at("dave") == "⚡🚀🍕");
    /* All three bombs are spent. */
    CHECK(furniture_all(storage).count("alice") == 0);

    /* Thrown at the place it lands on the holder, and there it is what he carries that goes: the ⚡,
       not the 🍕. Losing the ⚡ he came in with, the hold is worth less from then
       on, and what it made so far is put aside. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["alice"] = "💣💣";
        session.state().furniture["dave"] = "⚡🍕🥺💩";
        session.state().current->lightning_percent = 125;
        session.state().current->bolts = 1;
        return 0;
    });
    const FurnitureBurnResult onto = furniture_burn(storage, "alice", "💣", 100);
    CHECK(onto.status == FurnitureBurnStatus::burned);
    CHECK(onto.hit == "dave");
    CHECK(onto.blown == std::vector<std::string>{"⚡"});
    CHECK(onto.hit_furniture == "[]🍕🥺💩");
    CHECK(player_profile_of(storage, "dave", 100).lightning_percent == 0);
    const Holder after = storage.transaction([](StorageSession &session) { return *session.state().current; });
    CHECK(after.bolts == 0);
    CHECK(after.banked == earnings("dave", 100, 100, 125));
    CHECK(after.counted_from == 100);
    CHECK(after.since == 0);
    /* Nothing he carries is left: the 🥺 and the 💩 are at home, out of reach from here. */
    const FurnitureBurnResult again = furniture_burn(storage, "alice", "💣", 100);
    CHECK(again.hit == "dave");
    CHECK(again.blown.empty());
    CHECK(furniture_all(storage).at("dave") == "[]🍕🥺💩");
    /* The holder who throws one at his own place hits nobody. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["dave"] = "⚡💣";
        return 0;
    });
    const FurnitureBurnResult own = furniture_burn(storage, "dave", "💣", 100);
    CHECK(own.status == FurnitureBurnStatus::burned);
    CHECK(own.hit.empty());
    CHECK(furniture_all(storage).at("dave") == "⚡");

    /* A dud goes off in the thrower's hand: it spares the victim and takes one of what the thrower has
       with him, never what only hangs at his house. */
    RaidRules duds = quick_rides();
    duds.bomb_dud_percent = 100;
    storage.transaction([](StorageSession &session) {
        session.state().furniture["alice"] = "🥺🚀💣💣💣";
        return 0;
    });
    const FurnitureBurnResult dud = furniture_burn(storage, "alice", "💣", 200, duds);
    CHECK(dud.backfired);
    CHECK(dud.blown == std::vector<std::string>{"🚀"});
    CHECK(dud.shown == "🥺[][]💣💣");
    CHECK(furniture_all(storage).at("dave") == "⚡");
    /* On the way to a house it goes off on arrival, and with nothing on him he loses nothing. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 200, duds, RaidTargetKind::any, 0, "💣").status ==
            RaidStatus::started);
    const std::vector<RaidEvent> arrival = raid_due(storage, 205, duds);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].backfired);
    CHECK(arrival[0].blown.empty());
    CHECK(furniture_all(storage).at("bob") == "🍕[]🍕");
    CHECK(furniture_all(storage).at("alice") == "🥺[][][]💣");
}

TEST_CASE("an emoji hung while its owner is out is at home, even of a kind he would carry") {
    const TestPaths paths{"stayed-home-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":0},"quotes_added":{},)"
             << R"("furniture":{"alice":"⚡💣💣","bob":"⚡"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const ClaimRules claim{.cooldown_seconds = 0, .signs = {}, .lightning = 25};
    /* bob walks in with his one ⚡; alice brings him another, which is hung at his house. */
    CHECK(conquister_claim(storage, 2, "bob", 0, claim).entered_lightning == 125);
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, quick_rides(), RaidTargetKind::any, 0, "⚡").status ==
            RaidStatus::started);
    REQUIRE(raid_due(storage, 5, quick_rides()).size() == 1);
    REQUIRE(raid_due(storage, 10, quick_rides()).size() == 1);
    CHECK(furniture_all(storage).at("bob") == "⚡⚡");
    CHECK(player_profile_of(storage, "bob", 10).lightning_percent == 125);

    /* A bomb at his house finds the one that was hung there, not the one he has with him... */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 10, quick_rides(), RaidTargetKind::any, 0, "💣").status ==
            RaidStatus::started);
    const std::vector<RaidEvent> house = raid_due(storage, 15, quick_rides());
    REQUIRE(house.size() == 1);
    CHECK(house[0].blown == std::vector<std::string>{"⚡"});
    CHECK(furniture_all(storage).at("bob") == "⚡");
    CHECK(player_profile_of(storage, "bob", 15).lightning_percent == 125);
    REQUIRE(raid_due(storage, 20, quick_rides()).size() == 1);

    /* ...and one at the place finds the one he has with him, which lowers the hold. */
    const FurnitureBurnResult place = furniture_burn(storage, "alice", "💣", 20);
    CHECK(place.blown == std::vector<std::string>{"⚡"});
    CHECK(furniture_all(storage).count("bob") == 0);
    CHECK(player_profile_of(storage, "bob", 20).lightning_percent == 0);

    /* Once he has been home and left again, what was hung in his absence goes with him. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🚀";
        session.state().stayed["bob"] = {0};
        return 0;
    });
    REQUIRE(raid_start(storage, 2, "bob", "bob", 30, quick_rides()).status == RaidStatus::left_place);
    REQUIRE(raid_start(storage, 2, "bob", "alice", 30, quick_rides()).status == RaidStatus::started);
    CHECK(storage.transaction([](StorageSession &session) { return session.state().stayed.count("bob") == 0; }));
}

TEST_CASE("a ☢️ starts over the player whose house it lands on, and on the place the whole game") {
    const TestPaths paths{"nuke-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"bob","since":0},)"
             << R"("scores":{"alice":5,"bob":900,"carol":70,"dave":40},"quotes_added":{"bob":3},)"
             << R"("balloons":{"bob":2},"cooldowns":{"carol":99999},"ids":{"alice":1,"bob":2,"carol":3,"dave":4},)"
             << R"("telegram_ids":{"alice":11},"smeared":{"carol":99999},"stayed":{"bob":[0]},)"
             << R"("raids":[{"raider":"carol","target":"alice","arrive":50,"back":100,"arrived":false,)"
             << R"("loot":0,"gift":0,"gift_emoji":""}],)"
             << R"("furniture":{"alice":"☢️☢️","bob":"⚡","carol":"🥺","dave":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    /* On a house it is the one who lives there who starts over: bob, out of the place unpaid. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 10, quick_rides(), RaidTargetKind::any, 0, "☢️").status ==
            RaidStatus::started);
    const std::vector<RaidEvent> landed = raid_due(storage, 15, quick_rides());
    REQUIRE(landed.size() == 1);
    CHECK(landed[0].reset);
    {
        const ConquisterState local = storage.transaction([](StorageSession &session) { return session.state(); });
        CHECK(local.scores.at("bob") == 0);
        /* He starts again as everybody starts: with a 🎈. */
        CHECK(local.furniture.at("bob") == "🎈");
        CHECK_FALSE(local.current);
        CHECK(local.balloons.empty());
        CHECK(local.stayed.empty());
        /* Nobody else is touched. */
        CHECK(local.scores.at("carol") == 70);
        CHECK(local.furniture.at("carol") == "🥺");
        CHECK(local.raids.size() == 2);
        CHECK(read_json(paths.conquister + ".before-reset-15").at("scores").at("bob") == 900);
        std::filesystem::remove(paths.conquister + ".before-reset-15");
    }
    /* carol, on the road, is sent home with nothing by one that lands on her house. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["dave"] = "☢️";
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "dave", "carol", 16, quick_rides(), RaidTargetKind::any, 0, "☢️").status ==
            RaidStatus::started);
    const std::vector<RaidEvent> second = raid_due(storage, 21, quick_rides());
    CHECK(second.back().reset);
    std::filesystem::remove(paths.conquister + ".before-reset-21");
    static_cast<void>(raid_due(storage, 500, quick_rides()));
    storage.transaction([](StorageSession &session) {
        ConquisterState &state = session.state();
        CHECK(state.raids.empty());
        CHECK(state.scores.at("carol") == 0);
        state.scores["bob"] = 900;
        state.current = Holder{.user_id = 0, .username = "bob", .since = 0};
        return 0;
    });

    const FurnitureBurnResult dropped = furniture_burn(storage, "alice", "☢️", 500);
    CHECK(dropped.status == FurnitureBurnStatus::burned);
    CHECK(dropped.reset);
    const ConquisterState after = storage.transaction([](StorageSession &session) { return session.state(); });
    /* Everybody is still a player, at nothing. */
    CHECK(after.scores == Counters{{"alice", 0}, {"bob", 0}, {"carol", 0}, {"dave", 0}});
    CHECK_FALSE(after.current);
    CHECK(after.furniture.size() == 4);
    for (const char *player : {"alice", "bob", "carol", "dave"}) {
        CHECK(after.furniture.at(player) == "🎈");
    }
    CHECK(after.raids.empty());
    CHECK(after.balloons.empty());
    CHECK(after.cooldowns.empty());
    CHECK(after.smeared.empty());
    CHECK(after.stayed.empty());
    /* Who they are, where they live and what they quoted stay. */
    CHECK(after.ids == Counters{{"alice", 1}, {"bob", 2}, {"carol", 3}, {"dave", 4}});
    CHECK(after.telegram_ids == Counters{{"alice", 11}});
    CHECK(after.quotes_added == Counters{{"bob", 3}});
    /* The game as it was is kept beside the state. */
    const std::string backup = paths.conquister + ".before-reset-500";
    CHECK(read_json(backup).at("scores").at("bob") == 900);
    std::filesystem::remove(backup);
}

TEST_CASE("a 🐶 at home may catch a raider, who then takes nothing") {
    const TestPaths paths{"dog-test"};
    {
        /* bob is out, in the place: his dogs guard the house all the same. The balloons are worn out. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"bob","since":0},)"
             << R"("scores":{"alice":0,"bob":100,"carol":100},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":90000,"carol":90001},"balloons":{"bob":3,"carol":3},)"
             << R"("furniture":{"bob":"🐶🐶","carol":"🐶"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.loot_divisor = 1;
    /* Two dogs at fifty each never miss. */
    rules.dog_percent = 50;
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules).status == RaidStatus::started);
    std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::stolen);
    CHECK(arrival[0].intercepted);
    CHECK(arrival[0].loot == 0);
    CHECK(conquister_user(storage, "bob")->score == 100);
    /* The dogs are still there. */
    CHECK(furniture_all(storage).at("bob") == "🐶🐶");
    REQUIRE(raid_due(storage, 10, rules).size() == 1);

    /* With no chance to give, a dog is only an emoji. */
    rules.dog_percent = 0;
    REQUIRE(raid_start(storage, 0, "alice", "carol", 10, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 15, rules);
    REQUIRE(arrival.size() == 1);
    CHECK_FALSE(arrival[0].intercepted);
    CHECK(arrival[0].loot == 100);
}

TEST_CASE("a 📮 at home may send back what is thrown at the house") {
    const TestPaths paths{"mailbox-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":50,"bob":50},"quotes_added":{},)"
             << R"("furniture":{"alice":"🥺💩💣☢️🍕","bob":"📮📮🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    /* Two of them at fifty each never miss. */
    rules.mailbox_percent = 50;
    const auto thrown = [&](const char *emoji, std::int64_t now) {
        REQUIRE(raid_start(storage, 0, "alice", "bob", now, rules, RaidTargetKind::any, 0, emoji).status ==
                RaidStatus::started);
        const std::vector<RaidEvent> arrival = raid_due(storage, now + 5, rules);
        REQUIRE_FALSE(arrival.empty());
        static_cast<void>(raid_due(storage, now + 10, rules));
        return arrival[0];
    };

    /* The 💩 comes back: it is alice who is "lo smerdato", bob is clean. */
    const RaidEvent poo = thrown("💩", 0);
    CHECK(poo.sent_back);
    CHECK(poo.raider_smeared);
    CHECK_FALSE(poo.target_smeared);
    CHECK(smeared_all(storage, 5) == std::vector<std::string>{"alice"});

    /* The 💣 goes off at her house, among what she left there, and takes nothing of his. */
    const RaidEvent bomb = thrown("💣", 20);
    CHECK(bomb.sent_back);
    REQUIRE(bomb.blown.size() == 1);
    CHECK((bomb.blown[0] == "🥺" || bomb.blown[0] == "☢️"));
    CHECK(furniture_all(storage).at("bob") == "📮📮🍕");

    /* A present is not something thrown: it is delivered as ever. */
    const RaidEvent present = thrown("🍕", 40);
    CHECK_FALSE(present.sent_back);
    CHECK(furniture_all(storage).at("bob") == "📮📮🍕🍕");

    /* With no chance to give, a 📮 is only an emoji. */
    rules.mailbox_percent = 0;
    storage.transaction([](StorageSession &session) {
        session.state().furniture["alice"] = "💩";
        return 0;
    });
    const RaidEvent plain = thrown("💩", 60);
    CHECK_FALSE(plain.sent_back);
    CHECK(plain.target_smeared);
}

TEST_CASE("a 🥷 with the raider may take him past the 🎈 and the 🐶 of the house") {
    const TestPaths paths{"ninja-test"};
    {
        /* bob is at home behind a fresh 🎈 and two dogs that never miss. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":100,"carol":0,"dave":100},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":90000,"carol":1,"dave":90001},)"
             << R"("furniture":{"alice":"🥷🏿🥷","bob":"🎈🐶🐶","dave":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.loot_divisor = 1;
    rules.dog_percent = 50;
    /* Two of them at fifty each never fail. */
    rules.ninja_percent = 50;
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules).status == RaidStatus::started);
    std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].sneaked);
    CHECK_FALSE(arrival[0].balloon_held);
    CHECK_FALSE(arrival[0].balloon_popped);
    CHECK_FALSE(arrival[0].intercepted);
    CHECK(arrival[0].loot == 100);
    /* The 🎈 was not even touched. */
    CHECK(read_json(paths.conquister).at("balloons").empty());
    REQUIRE(raid_due(storage, 10, rules).size() == 1);

    /* Two 🔊 at the house take all of that chance away: the alarm gives her away, and the dogs do the rest. */
    storage.transaction([](StorageSession &session) {
        session.state().scores["bob"] = 100;
        session.state().furniture["bob"] = "🐶🐶🔊🔊";
        return 0;
    });
    rules.alarm_percent = 50;
    REQUIRE(raid_start(storage, 0, "alice", "bob", 10, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 15, rules);
    REQUIRE(arrival.size() == 1);
    CHECK_FALSE(arrival[0].sneaked);
    CHECK(arrival[0].alarmed);
    CHECK(arrival[0].intercepted);
    REQUIRE(raid_due(storage, 20, rules).size() == 1);
    rules.alarm_percent = 0;

    /* Without one, the dogs are there for her. */
    storage.transaction([](StorageSession &session) {
        session.state().scores["bob"] = 100;
        session.state().furniture["bob"] = "🐶🐶";
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "carol", "bob", 20, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 25, rules);
    REQUIRE(arrival.size() == 1);
    CHECK_FALSE(arrival[0].sneaked);
    CHECK_FALSE(arrival[0].alarmed);
    CHECK(arrival[0].intercepted);
    REQUIRE(raid_due(storage, 30, rules).size() == 1);

    /* The same at the place: past the holder's 🎈, which stays as worn as it was. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🎈";
        session.state().balloons["bob"] = 2;
        session.state().current = Holder{.user_id = 0, .username = "bob", .since = 30};
        return 0;
    });
    const ClaimResult quiet = conquister_claim(storage, 0, "alice", 30,
                                               ClaimRules{.cooldown_seconds = 0, .signs = {}, .ninja = 50});
    CHECK(quiet.status == ClaimStatus::taken);
    CHECK(quiet.sneaked);
    CHECK_FALSE(quiet.balloon_popped);
    CHECK(quiet.previous_username == "bob");
    CHECK(read_json(paths.conquister).at("balloons").at("bob") == 2);
    CHECK(furniture_all(storage).at("bob") == "🎈");
    storage.transaction([](StorageSession &session) {
        session.state().current.reset();
        return 0;
    });

    /* Where nothing guards the house there is nothing to slip past. */
    REQUIRE(raid_start(storage, 0, "alice", "dave", 30, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 35, rules);
    REQUIRE(arrival.size() == 1);
    CHECK_FALSE(arrival[0].sneaked);
    CHECK(arrival[0].loot == 100);
}

TEST_CASE("a 🌀 flings a player a year of road away from everybody and from the place") {
    const TestPaths paths{"vortex-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"bob","since":0},)"
             << R"("scores":{"alice":0,"bob":10,"carol":10},"quotes_added":{},)"
             << R"("furniture":{"alice":"🌀🌀","bob":"🚀🍕","carol":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    /* Thrown at the place it lands on the holder: out he goes, paid for his hold, and far away. */
    const FurnitureBurnResult thrown = furniture_burn(storage, "alice", "🌀", 100);
    CHECK(thrown.flung);
    CHECK(thrown.hit == "bob");
    CHECK(player_profile_of(storage, "bob", 100).flung);
    CHECK(player_profile_of(storage, "bob", 100).place == Whereabouts::home);
    CHECK(conquister_user(storage, "bob")->score > 10);
    /* The place is a year away for him, to hold or to throw at. */
    CHECK(conquister_claim(storage, 0, "bob", 110).status == ClaimStatus::too_far);
    CHECK(furniture_burn(storage, "bob", "🍕", 110).status == FurnitureBurnStatus::too_far);
    CHECK(palle_burn(storage, "bob", 1).status == BurnStatus::too_far);
    /* And so is everybody, from him or to him. */
    CHECK(raid_start(storage, 0, "carol", "bob", 120, quick_rides()).seconds == flung_seconds);
    CHECK(raid_start(storage, 0, "bob", "alice", 120, quick_rides()).seconds == flung_seconds);
    /* Nobody else is any further than he was. */
    CHECK_FALSE(player_profile_of(storage, "carol", 120).flung);

    /* At a house it does the same to whoever lives there. */
    storage.transaction([](StorageSession &session) {
        session.state().raids.clear();
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "carol", 200, quick_rides(), RaidTargetKind::any, 0, "🌀").status ==
            RaidStatus::started);
    const std::vector<RaidEvent> arrival = raid_due(storage, 205, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].flung);
    CHECK(player_profile_of(storage, "carol", 205).flung);
}

TEST_CASE("a 🏴‍☠️ with the raider may carry off an emoji that is at the house") {
    const TestPaths paths{"pirate-test"};
    {
        /* bob holds the place: his ⚡ is with him, his 🎈 too. Only the 🍕 is at home to take. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"bob","since":0},)"
             << R"("scores":{"alice":0,"bob":100,"carol":100,"dave":0},"quotes_added":{},)"
             << R"("furniture":{"alice":"🏴‍☠️🏴‍☠️","bob":"⚡🎈🍕","carol":"🎈",)"
             << R"("dave":"🏴‍☠️🏴‍☠️🍕🍕🍕🍕🍕🍕🍕🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    /* Two of them at fifty each never fail. */
    rules.pirate_percent = 50;
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules).status == RaidStatus::started);
    std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].boarded == "🍕");
    CHECK(furniture_all(storage).at("bob") == "⚡🎈");
    CHECK(furniture_all(storage).at("alice") == "🏴‍☠️🏴‍☠️🍕");
    CHECK(arrival[0].raider_emoji == "🏴‍☠️🏴‍☠️🍕");
    REQUIRE(raid_due(storage, 10, rules).size() == 1);

    /* Nothing left there but what he carries: nothing to take. A 🎈 is never taken. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 10, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 15, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].boarded.empty());
    REQUIRE(raid_due(storage, 20, rules).size() == 1);
    storage.transaction([](StorageSession &session) {
        session.state().balloons["carol"] = 3;
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "carol", 20, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 25, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].boarded.empty());
    CHECK(furniture_all(storage).at("carol") == "🎈");
    REQUIRE(raid_due(storage, 30, rules).size() == 1);

    /* A name with no room carries nothing off. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕";
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "dave", "bob", 30, rules).status == RaidStatus::started);
    arrival = raid_due(storage, 35, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].boarded.empty());
    CHECK(furniture_all(storage).at("bob") == "🍕");
}

TEST_CASE("a 💦 leaves a child on the way, born on the name it landed on") {
    const TestPaths paths{"seed-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"carol","since":0},)"
             << R"("scores":{"alice":0,"bob":0,"carol":0},"quotes_added":{},"telegram_ids":{"alice":1},)"
             << R"("furniture":{"alice":"💦💦💦","bob":"🍕","carol":"🎈⚡"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.pregnancy_seconds = 100;
    rules.child_stage_seconds = 100000;
    rules.furniture_limit = 2;

    /* At a house nothing shows when it lands. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules, RaidTargetKind::any, 0, "💦").status ==
            RaidStatus::started);
    std::vector<RaidEvent> events = raid_due(storage, 5, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RaidEvent::Kind::delivered);
    CHECK(events[0].expecting == 100);
    CHECK(furniture_all(storage).at("bob") == "🍕");
    REQUIRE(raid_due(storage, 10, rules).size() == 1);
    CHECK(raid_due(storage, 104, rules).empty());
    /* Its time come, the newborn takes the empty slot: a boy or a girl. */
    events = raid_due(storage, 105, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RaidEvent::Kind::born);
    CHECK(events[0].raider == "alice");
    CHECK(events[0].raider_on_telegram);
    CHECK(events[0].target == "bob");
    CHECK(events[0].gift_emoji == "👶");
    CHECK(events[0].blown.empty());
    CHECK(furniture_all(storage).at("bob") == "🍕👶");
    CHECK(raid_due(storage, 106, rules).empty());

    /* On a full name the next one takes the place of another emoji, never of the first child. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 200, rules, RaidTargetKind::any, 0, "💦").status ==
            RaidStatus::started);
    REQUIRE(raid_due(storage, 205, rules).size() == 1);
    REQUIRE(raid_due(storage, 210, rules).size() == 1);
    events = raid_due(storage, 305, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].blown == std::vector<std::string>{"🍕"});
    CHECK(furniture_all(storage).at("bob") == "👶👶");

    /* With the owner out there is nobody for it to land on: the raider takes it back home. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["alice"] = "💦💦";
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "carol", 310, rules, RaidTargetKind::any, 0, "💦").status ==
            RaidStatus::started);
    events = raid_due(storage, 315, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].nobody_home);
    CHECK(events[0].expecting == 0);
    CHECK(furniture_all(storage).at("alice") == "[]💦");
    events = raid_due(storage, 320, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RaidEvent::Kind::returned);
    CHECK(events[0].gift_emoji == "💦");
    CHECK(furniture_all(storage).at("alice") == "💦💦");
    CHECK(storage.transaction([](StorageSession &session) { return session.state().pregnancies.empty(); }));

    /* Thrown at the place it is the holder who is expecting; her 🎈 is never the slot taken. */
    const FurnitureBurnResult onto = furniture_burn(storage, "alice", "💦", 400, rules);
    CHECK(onto.hit == "carol");
    CHECK(onto.expecting == 100);
    events = raid_due(storage, 500, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].target == "carol");
    CHECK(events[0].blown == std::vector<std::string>{"⚡"});
    CHECK(furniture_all(storage).at("carol") == "🎈👶");
}

TEST_CASE("a child grows through its ages where it was born, then leaves, and nothing takes it before") {
    const TestPaths paths{"child-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":100},"quotes_added":{},)"
             << R"("furniture":{"alice":"🏴‍☠️🏴‍☠️","bob":"👶"},)"
             << R"("children":[{"owner":"bob","slot":0,"male":true,"born":0}]})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.child_stage_seconds = 100;
    rules.pirate_percent = 50;
    rules.adult_per_second = 3;
    const auto score = [&storage] {
        return storage.transaction([](StorageSession &session) { return session.state().scores.at("bob"); });
    };

    /* It is not his to hand over, and nothing can be hung in its place. */
    CHECK(raid_start(storage, 0, "bob", "alice", 10, rules, RaidTargetKind::any, 0, "👶").status ==
          RaidStatus::no_such_emoji);
    CHECK(furniture_buy(storage, "bob", "🍕", 1, 0, 10, 10).status == FurnitureStatus::child_there);
    /* Nor is it there for a pirate to take. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 10, rules).status == RaidStatus::started);
    std::vector<RaidEvent> events = raid_due(storage, 15, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].boarded.empty());
    CHECK(furniture_all(storage).at("bob") == "👶");
    REQUIRE(raid_due(storage, 20, rules).size() == 1);

    /* Moved, it is the same child in another slot, and it goes on growing there. */
    CHECK(furniture_move(storage, "bob", 1, 3, 10, 30).status == FurnitureMoveStatus::moved);
    CHECK(furniture_all(storage).at("bob") == "[][]👶");
    const std::int64_t before = score();
    CHECK(raid_due(storage, 100, rules).empty());
    CHECK(furniture_all(storage).at("bob") == "[][]👦");
    CHECK(raid_due(storage, 200, rules).empty());
    CHECK(furniture_all(storage).at("bob") == "[][]👨");
    /* Only as a grown-up does it earn: nothing before, so much a second while it lasts. */
    CHECK(score() == before);
    CHECK(raid_due(storage, 240, rules).empty());
    CHECK(score() == before + 120);
    CHECK(raid_due(storage, 399, rules).empty());
    CHECK(furniture_all(storage).at("bob") == "[][]👴");
    /* The whole of that age is paid, and not a second of the next. */
    CHECK(score() == before + 300);
    /* A whole life lived, it leaves by itself and the slot is free again. */
    events = raid_due(storage, 400, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RaidEvent::Kind::gone);
    CHECK(events[0].target == "bob");
    CHECK(events[0].gift_emoji == "👴");
    CHECK(furniture_all(storage).count("bob") == 0);
    CHECK(storage.transaction([](StorageSession &session) { return session.state().children.empty(); }));

    /* The one way to part with a child is to leave it at the place: it is gone, and so is what it would
       have earned. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕👩";
        session.state().children.push_back(
            Child{.owner = "bob", .slot = 1, .male = false, .born = 1000, .paid = 0, .courted = false});
        return 0;
    });
    CHECK(furniture_burn(storage, "bob", "👶", 1250, rules).status == FurnitureBurnStatus::not_owned);
    const FurnitureBurnResult left = furniture_burn(storage, "bob", "👩", 1250, rules);
    CHECK(left.status == FurnitureBurnStatus::burned);
    CHECK(left.abandoned);
    CHECK(left.shown == "🍕");
    CHECK(storage.transaction([](StorageSession &session) { return session.state().children.empty(); }));
}

TEST_CASE("every 🐔 on a name lays palle for its owner minute after minute") {
    const TestPaths paths{"hen-test"};
    {
        /* alice holds the place: her hens lay at home all the same. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"alice","since":1000},)"
             << R"("scores":{"alice":0,"bob":5,"carol":7},"quotes_added":{},)"
             << R"("furniture":{"alice":"🐔🐔🐔","bob":"🍕🐔","carol":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.hen_per_minute = 2;
    const auto score = [&storage](const char *player) {
        return storage.transaction([player](StorageSession &session) { return session.state().scores.at(player); });
    };
    /* The first round only starts the clock. */
    CHECK(raid_due(storage, 1000, rules).empty());
    CHECK(score("alice") == 0);
    /* Whole minutes only: the fifty-nine seconds wait. */
    static_cast<void>(raid_due(storage, 1059, rules));
    CHECK(score("bob") == 5);
    static_cast<void>(raid_due(storage, 1060, rules));
    CHECK(score("alice") == 6);
    CHECK(score("bob") == 7);
    CHECK(score("carol") == 7);
    /* Time the bot missed is made up for, and the seconds left over are not lost. */
    static_cast<void>(raid_due(storage, 1330, rules));
    CHECK(score("alice") == 6 + 4 * 6);
    static_cast<void>(raid_due(storage, 1360, rules));
    CHECK(score("alice") == 6 + 5 * 6);
    CHECK(player_profile_of(storage, "alice", 1360).hens == 3);
    /* With nothing to lay, a 🐔 is only an emoji. */
    rules.hen_per_minute = 0;
    static_cast<void>(raid_due(storage, 5000, rules));
    CHECK(score("bob") == 5 + 6 * 2);
}

TEST_CASE("a grown-up girl and a grown-up boy of the same house may have a child of their own") {
    const TestPaths paths{"mating-test"};
    {
        /* On bob's name a boy and a girl of the same age; on carol's only a girl. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"bob":0,"carol":0},"quotes_added":{},)"
             << R"("furniture":{"bob":"👶👶","carol":"👶"},)"
             << R"("children":[{"owner":"bob","slot":0,"male":true,"born":0},)"
             << R"({"owner":"bob","slot":1,"male":false,"born":0},)"
             << R"({"owner":"carol","slot":0,"male":false,"born":0}]})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.child_stage_seconds = 1000;
    rules.pregnancy_seconds = 100;
    rules.mating_percent = 100;
    const auto pregnancies = [&storage] {
        return storage.transaction([](StorageSession &session) { return session.state().pregnancies; });
    };
    /* Not before they are grown up. */
    CHECK(raid_due(storage, 1500, rules).empty());
    CHECK(pregnancies().empty());
    /* Then once, and only where there is a boy too. */
    CHECK(raid_due(storage, 2000, rules).empty());
    REQUIRE(pregnancies().size() == 1);
    CHECK(pregnancies()[0].mother == "bob");
    CHECK(pregnancies()[0].father.empty());
    CHECK(pregnancies()[0].due == 2100);
    CHECK(raid_due(storage, 2050, rules).empty());
    CHECK(pregnancies().size() == 1);
    /* The child of the house is born like any other. */
    const std::vector<RaidEvent> events = raid_due(storage, 2100, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RaidEvent::Kind::born);
    CHECK(events[0].raider.empty());
    CHECK(events[0].target == "bob");
    CHECK(furniture_all(storage).at("bob") == "👨👩👶");
    CHECK(furniture_all(storage).at("carol") == "👩");
}

TEST_CASE("every 🪤 at the house makes a raider's ride home longer") {
    const TestPaths paths{"trap-test"};
    {
        /* A thousand units apart: a thousand seconds each way at one unit a second. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":100,"carol":100},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":1000,"carol":1000},)"
             << R"("furniture":{"alice":"🍕","bob":"🪤🪤🍕","carol":"🪤"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const RaidRules rules{.loot_divisor = 50, .travel_divisor = 1, .signs = {}, .trap_percent = 20};

    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules).seconds == 1000);
    std::vector<RaidEvent> events = raid_due(storage, 1000, rules);
    REQUIRE(events.size() == 1);
    /* Two of them: two fifths more road home, and she still takes what she came for. */
    CHECK(events[0].trapped == 400);
    CHECK(events[0].seconds == 1400);
    CHECK(events[0].loot > 0);
    CHECK(raid_due(storage, 2399, rules).empty());
    REQUIRE(raid_due(storage, 2400, rules).size() == 1);

    /* A present does not spring them. */
    REQUIRE(raid_start(storage, 0, "alice", "carol", 3000, rules, RaidTargetKind::any, 0, "🍕").status ==
            RaidStatus::started);
    events = raid_due(storage, 4000, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RaidEvent::Kind::delivered);
    CHECK(events[0].trapped == 0);
    CHECK(events[0].seconds == 1000);
}

TEST_CASE("a 🧊 freezes whoever it hits: no place and no setting off until he thaws") {
    const TestPaths paths{"ice-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"carol","since":0},)"
             << R"("scores":{"alice":0,"bob":10,"carol":10},"quotes_added":{},)"
             << R"("furniture":{"alice":"🧊🧊","bob":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.frozen_seconds = 300;
    /* At a house it freezes whoever lives there. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules, RaidTargetKind::any, 0, "🧊").status ==
            RaidStatus::started);
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].froze == 300);
    CHECK(player_profile_of(storage, "bob", 5).frozen_for == 300);
    REQUIRE(raid_due(storage, 10, rules).size() == 1);
    const ClaimResult claim = conquister_claim(storage, 0, "bob", 105);
    CHECK(claim.status == ClaimStatus::frozen);
    CHECK(claim.penalty_seconds == 200);
    const RaidResult leaving = raid_start(storage, 0, "bob", "alice", 105, rules);
    CHECK(leaving.status == RaidStatus::frozen);
    CHECK(leaving.seconds == 200);
    /* He can still do what is done at home. */
    CHECK(furniture_buy(storage, "bob", "🐟", 0, 0, 10, 105).status == FurnitureStatus::bought);
    /* Thawed, he is free again, and nothing of it is left on file. */
    CHECK(raid_due(storage, 305, rules).empty());
    CHECK(player_profile_of(storage, "bob", 305).frozen_for == 0);
    CHECK(storage.transaction([](StorageSession &session) { return session.state().frozen.empty(); }));
    CHECK(raid_start(storage, 0, "bob", "alice", 305, rules).status == RaidStatus::started);

    /* Each 🔥 he has with him melts a share of it, and enough of them melt it all. */
    rules.fire_percent = 20;
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🔥🔥";
        session.state().furniture["alice"] = "🧊🧊🧊🧊";
        session.state().raids.clear();
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "bob", 310, rules, RaidTargetKind::any, 0, "🧊").status ==
            RaidStatus::started);
    std::vector<RaidEvent> melted = raid_due(storage, 315, rules);
    REQUIRE(melted.size() == 1);
    CHECK(melted[0].froze == 180);
    CHECK(melted[0].melted);
    static_cast<void>(raid_due(storage, 320, rules));
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🔥🔥🔥🔥🔥";
        session.state().frozen.clear();
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "bob", 330, rules, RaidTargetKind::any, 0, "🧊").status ==
            RaidStatus::started);
    melted = raid_due(storage, 335, rules);
    REQUIRE(melted.size() == 1);
    CHECK(melted[0].froze == 0);
    CHECK(player_profile_of(storage, "bob", 335).frozen_for == 0);
    static_cast<void>(raid_due(storage, 340, rules));
    rules.fire_percent = 0;

    /* Thrown at the place it freezes the holder, who stays where he is. */
    const FurnitureBurnResult onto = furniture_burn(storage, "alice", "🧊", 400, rules);
    CHECK(onto.hit == "carol");
    CHECK(onto.froze == 300);
    CHECK(player_profile_of(storage, "carol", 400).place == Whereabouts::conquister);
    CHECK(player_profile_of(storage, "carol", 400).frozen_for == 300);
}

TEST_CASE("every ⏳ with the claimer takes a share off the penalty of a failed attempt") {
    const TestPaths paths{"hourglass-test"};
    {
        /* alice holds the place behind a fresh 🎈 that cannot pop on the first attempt here: each of the
           three tries below is made against a balloon nobody has touched yet. */
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":0,"username":"alice","since":0},"scores":{},"quotes_added":{},)"
             << R"("furniture":{"alice":"🎈","bob":"⏳⏳⏳","carol":"⏳⏳⏳⏳⏳⏳⏳⏳⏳⏳"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const ClaimRules rules{.cooldown_seconds = 300, .signs = {}, .hourglass = 10};
    const auto failed = [&](const char *who, std::int64_t now) -> std::optional<ClaimResult> {
        for (int attempt = 0; attempt < 40; ++attempt) {
            storage.transaction([who](StorageSession &session) {
                ConquisterState &state = session.state();
                state.current = Holder{.user_id = 0, .username = "alice", .since = 0};
                state.balloons.clear();
                state.cooldowns.erase(who);
                return 0;
            });
            ClaimResult result = conquister_claim(storage, 0, who, now, rules);
            if (result.status == ClaimStatus::defended) {
                return result;
            }
        }
        return std::nullopt;
    };
    /* Three of them: three tenths off the five minutes. */
    const std::optional<ClaimResult> bob = failed("bob", 100);
    REQUIRE(bob);
    CHECK(bob->penalty_seconds == 210);
    CHECK(conquister_claim(storage, 0, "bob", 200, rules).status == ClaimStatus::cooldown);
    /* Ten of them: no wait at all. */
    const std::optional<ClaimResult> carol = failed("carol", 100);
    REQUIRE(carol);
    CHECK(carol->penalty_seconds == 0);
    CHECK(storage.transaction([](StorageSession &session) { return session.state().cooldowns.count("carol"); }) == 0);
}

TEST_CASE("a pile of poo is thrown, not hung: a full name takes it and keeps nothing") {
    const TestPaths paths{"poo-throw-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":10,"bob":10},"quotes_added":{},)"
             << R"("furniture":{"alice":"💩🍕","bob":"🐝🐝🐝🐝🐝🐝🐝🐝🐝🐝"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, quick_rides(), RaidTargetKind::any, 0, "💩").status ==
            RaidStatus::started);
    CHECK(furniture_all(storage).at("alice") == "[]🍕");
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::delivered);
    CHECK(arrival[0].gift_emoji == "💩");
    CHECK_FALSE(arrival[0].no_room);
    CHECK(furniture_all(storage).at("bob") == "🐝🐝🐝🐝🐝🐝🐝🐝🐝🐝");
    /* Hit, bob is "lo smerdato" for a day from the splat; alice, who threw it, is not. */
    CHECK(arrival[0].target_smeared);
    CHECK_FALSE(arrival[0].raider_smeared);
    CHECK(player_profile_of(storage, "bob", 5).smeared);
    CHECK_FALSE(player_profile_of(storage, "alice", 5).smeared);
    CHECK(smeared_all(storage, 5) == std::vector<std::string>{"bob"});
    /* Splattered: nothing comes back. */
    const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
    REQUIRE(home.size() == 1);
    CHECK(home[0].gift_emoji.empty());
    CHECK(home[0].target_smeared);
    CHECK(furniture_all(storage).at("alice") == "[]🍕");
    CHECK(player_profile_of(storage, "bob", 5 + 86399).smeared);
    CHECK_FALSE(player_profile_of(storage, "bob", 5 + 86400).smeared);
    CHECK(smeared_all(storage, 5 + 86400).empty());
}

TEST_CASE("an emoji on its way to somebody still counts for the price") {
    const TestPaths paths{"furniture-transit-price-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":10,"bob":10,"carol":100000},"quotes_added":{},)"
             << R"("furniture":{"alice":"🍕"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, quick_rides(), RaidTargetKind::any, 0, "🍕").status ==
            RaidStatus::started);
    const FurnitureResult bought = furniture_buy(storage, "carol", "🍕", 0, 1000, 10, 1);
    CHECK(bought.copies == 1);
    CHECK(bought.charged == 2000);
}

TEST_CASE("an emoji is carried to another player, or burnt at the place") {
    const TestPaths paths{"emoji-gift-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":10,"bob":10},"quotes_added":{},)"
             << R"("furniture":{"alice":"🍕🎈"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const auto hung = [&storage](const std::string &player) {
        const Authors all = furniture_all(storage);
        const auto found = all.find(player);
        return found == all.end() ? std::string{} : found->second;
    };

    /* Only an emoji he has. */
    CHECK(raid_start(storage, 0, "alice", "bob", 0, quick_rides(), RaidTargetKind::any, 0, "🐟").status ==
          RaidStatus::no_such_emoji);

    /* It leaves her name as she sets off, a hole where it hung. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, quick_rides(), RaidTargetKind::any, 0, "🍕").status ==
            RaidStatus::started);
    CHECK(hung("alice") == "[]🎈");
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::delivered);
    CHECK(arrival[0].gift_emoji == "🍕");
    CHECK_FALSE(arrival[0].no_room);
    CHECK(arrival[0].target_emoji == "🍕");
    CHECK(hung("bob") == "🍕");
    CHECK(conquister_user(storage, "bob")->score == 10);
    const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
    REQUIRE(home.size() == 1);
    CHECK(home[0].gift_emoji.empty());

    /* A full name is refused at the start... */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕🍕🍕🍕🍕🍕🍕🍕🍕🍕";
        return 0;
    });
    CHECK(raid_start(storage, 0, "alice", "bob", 11, quick_rides(), RaidTargetKind::any, 0, "🎈").status ==
          RaidStatus::no_room);
    CHECK(hung("alice") == "[]🎈");

    /* ...and one that fills up on the way sends it back home with her. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕";
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "bob", 20, quick_rides(), RaidTargetKind::any, 0, "🎈").status ==
            RaidStatus::started);
    CHECK(hung("alice").empty());
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕🍕🍕🍕🍕🍕🍕🍕🍕🍕";
        return 0;
    });
    const std::vector<RaidEvent> refused = raid_due(storage, 25, quick_rides());
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].no_room);
    const std::vector<RaidEvent> back = raid_due(storage, 30, quick_rides());
    REQUIRE(back.size() == 1);
    CHECK(back[0].gift_emoji == "🎈");
    CHECK(hung("alice") == "🎈");

    /* While it travels, her last empty slot stays kept for it: no gift can take it. Turning around,
       it finds its way back home. */
    storage.transaction([](StorageSession &session) {
        session.state().furniture["bob"] = "🍕";
        session.state().furniture["carol"] = "🧀";
        return 0;
    });
    REQUIRE(raid_start(storage, 0, "alice", "bob", 40, quick_rides(), RaidTargetKind::any, 0, "🎈").status ==
            RaidStatus::started);
    /* Away from home she cannot hang anything on her name. */
    CHECK(furniture_buy(storage, "alice", "🐝", 0, 0, 10, 0).status == FurnitureStatus::not_home);
    storage.transaction([](StorageSession &session) {
        session.state().furniture["alice"] = "🐝🐝🐝🐝🐝🐝🐝🐝🐝";
        return 0;
    });
    CHECK(raid_start(storage, 0, "carol", "alice", 41, quick_rides(), RaidTargetKind::any, 0, "🧀").status ==
          RaidStatus::no_room);
    const RaidResult turned = raid_start(storage, 0, "alice", "alice", 42, quick_rides());
    REQUIRE(turned.status == RaidStatus::coming_home);
    const std::vector<RaidEvent> returned = raid_due(storage, 42 + turned.seconds, quick_rides());
    REQUIRE(returned.size() == 1);
    CHECK(returned[0].gift_emoji == "🎈");
    CHECK(hung("alice") == "🐝🐝🐝🐝🐝🐝🐝🐝🐝🎈");
    CHECK(furniture_buy(storage, "alice", "🚀", 0, 0, 10, 0).status == FurnitureStatus::full);

    /* Brought to the place, an emoji is gone: the first copy, leaving a hole. */
    const FurnitureBurnResult burnt = furniture_burn(storage, "alice", "🐝");
    CHECK(burnt.status == FurnitureBurnStatus::burned);
    CHECK(burnt.shown == "[]🐝🐝🐝🐝🐝🐝🐝🐝🎈");
    CHECK(furniture_burn(storage, "alice", "🎺").status == FurnitureBurnStatus::not_owned);
    CHECK(furniture_burn(storage, "bob", "🍕").shown.empty());
    CHECK(hung("bob").empty());
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
    const RaidRules slow{.loot_divisor = 50, .travel_divisor = 1, .signs = {}};

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

TEST_CASE("a hold saved with a whole multiplier keeps it as a percent") {
    const TestPaths paths{"lightning-legacy-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0,"multiplier":3},"scores":{},)"
             << R"("quotes_added":{},"balloons":{"alice":3}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const ClaimResult kicked = conquister_claim(storage, 2, "bob", 1000);
    CHECK(kicked.lightning == 300);
    CHECK(kicked.earned == earnings("alice", 1000, 1000, 300));
}

TEST_CASE("a 🦖 at the house eats one of the emoji the raider has with him") {
    const TestPaths paths{"dino-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"bob":100},"quotes_added":{},)"
             << R"("ids":{"alice":0,"bob":1000},)"
             << R"("furniture":{"alice":"🎈🏴‍☠️🍕","bob":"🦖"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    const RaidRules rules{.loot_divisor = 50, .travel_divisor = 1, .signs = {}, .dino_percent = 100};

    /* The 🏴‍☠️ goes with her and is eaten; the 🍕 stayed at home, and no 🎈 is ever eaten. */
    REQUIRE(raid_start(storage, 0, "alice", "bob", 0, rules).status == RaidStatus::started);
    std::vector<RaidEvent> events = raid_due(storage, 1000, rules);
    REQUIRE(events.size() == 1);
    CHECK(events[0].eaten == "🏴‍☠️");
    CHECK(furniture_all(storage).at("alice") == "🎈[]🍕");
    REQUIRE(raid_due(storage, 2000, rules).size() == 1);

    /* With nothing left to eat, it finds nothing. */
    REQUIRE(raid_start(storage, 3000, "alice", "bob", 0, rules).status == RaidStatus::started);
    events = raid_due(storage, 4000, rules);
    REQUIRE(!events.empty());
    CHECK(events[0].kind == RaidEvent::Kind::stolen);
    CHECK(events[0].eaten.empty());
    CHECK(furniture_all(storage).at("alice") == "🎈[]🍕");
}

TEST_CASE("every 🧲 the raider has with him wins back a share of what the 🥺 spare") {
    const TestPaths paths{"magnet-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":0,"lucy":100},"quotes_added":{},)"
             << R"("ids":{"alice":0,"lucy":90000},"balloons":{"lucy":3},)"
             << R"("furniture":{"alice":"🧲🧲🧲","lucy":"🥺🥺🥺🥺🥺🥺🥺🥺🥺🥺"}})";
    }
    Storage storage{paths.conquister, paths.quotes};
    RaidRules rules = quick_rides();
    rules.loot_divisor = 1;
    rules.pleading_percent = 10;
    rules.magnet_percent = 10;

    /* Ten 🥺 would spare her everything: three 🧲 take thirty points off. */
    REQUIRE(raid_start(storage, 0, "alice", "lucy", 0, rules).status == RaidStatus::started);
    std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].pleaded_percent == 70);
    CHECK(arrival[0].spared == 70);
    CHECK(arrival[0].loot == 30);
}
