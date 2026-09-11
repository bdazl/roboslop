import gorden.agent.observation;
import roboslop.ecs;
import roboslop.scene.transform;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

TEST_CASE(
    "buildObservation lists named entities in range sorted by distance", "[agent][observation]"
) {
    roboslop::World w;
    const auto robot = w.create();
    w.emplace<roboslop::Transform>(robot, roboslop::Transform{.position = {0.0F, 0.0F, 0.0F}});
    w.emplace<gorden::Named>(robot, gorden::Named{.name = "robot"});

    const auto player = w.create();
    w.emplace<roboslop::Transform>(player, roboslop::Transform{.position = {4.0F, 1.0F, 0.0F}});
    w.emplace<gorden::Named>(player, gorden::Named{.name = "player"});

    const auto crate = w.create();
    w.emplace<roboslop::Transform>(crate, roboslop::Transform{.position = {1.0F, 0.0F, 1.0F}});
    w.emplace<gorden::Named>(crate, gorden::Named{.name = "crate-1"});

    const auto farAway = w.create();
    w.emplace<roboslop::Transform>(farAway, roboslop::Transform{.position = {50.0F, 0.0F, 0.0F}});
    w.emplace<gorden::Named>(farAway, gorden::Named{.name = "generator"});

    const auto unnamed = w.create();
    w.emplace<roboslop::Transform>(unnamed, roboslop::Transform{.position = {0.5F, 0.0F, 0.0F}});

    const auto obs = gorden::buildObservation(w, robot, player, 10.0F);
    REQUIRE(obs.robotPosition.x == 0.0F);
    REQUIRE(obs.playerPosition.x == 4.0F);
    REQUIRE(obs.nearby.size() == 2);
    REQUIRE(obs.nearby[0].name == "crate-1");
    REQUIRE(obs.nearby[1].name == "player");
    REQUIRE(obs.nearby[1].distance == Catch::Approx(4.1231F).margin(1e-3F));
}

TEST_CASE("observationToJson is stable JSON the model can read", "[agent][observation]") {
    gorden::Observation obs;
    obs.robotPosition = {1.234F, 0.0F, -2.0F};
    obs.playerPosition = {0.0F, 1.0F, 0.0F};
    obs.robotMoving = true;
    obs.nearby.push_back({.name = "crate-1", .position = {1.0F, 0.0F, 1.0F}, .distance = 1.41F});
    obs.recentEvents.emplace_back("move_completed: arrived");
    obs.playerMessage = "hello";

    const auto j = nlohmann::json::parse(gorden::observationToJson(obs));
    REQUIRE(j["robot"]["position"]["x"] == 1.23);
    REQUIRE(j["robot"]["moving"] == true);
    REQUIRE(j["player"]["position"]["y"] == 1.0);
    REQUIRE(j["nearby"].size() == 1);
    REQUIRE(j["nearby"][0]["name"] == "crate-1");
    REQUIRE(j["nearby"][0]["distance"] == 1.4);
    REQUIRE(j["events"][0] == "move_completed: arrived");
    REQUIRE(j["player_message"] == "hello");
}

TEST_CASE("observationToJson omits player_message when empty", "[agent][observation]") {
    const auto j = nlohmann::json::parse(gorden::observationToJson(gorden::Observation{}));
    REQUIRE_FALSE(j.contains("player_message"));
    REQUIRE(j["nearby"].empty());
}

TEST_CASE("observations carry visible state, not details", "[agent][observation]") {
    roboslop::World w;
    const auto robot = w.create();
    w.emplace<roboslop::Transform>(robot, roboslop::Transform{});
    const auto door = w.create();
    w.emplace<roboslop::Transform>(door, roboslop::Transform{.position = {3.0F, 0.0F, 0.0F}});
    w.emplace<gorden::Named>(door, gorden::Named{.name = "Exit door"});
    w.emplace<gorden::Inspectable>(
        door, gorden::Inspectable{.state = "locked", .detail = "held by an interlock"}
    );

    const auto obs = gorden::buildObservation(w, robot, robot, 10.0F);
    REQUIRE(obs.nearby.size() == 1);
    REQUIRE(obs.nearby[0].state == "locked");
    REQUIRE(obs.nearby[0].entity == door);

    const auto text = gorden::observationToJson(obs);
    const auto j = nlohmann::json::parse(text);
    REQUIRE(j["nearby"][0]["state"] == "locked");
    REQUIRE_FALSE(text.contains("interlock"));
}
