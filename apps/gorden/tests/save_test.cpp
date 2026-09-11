import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.first_room;
import gorden.save;
import roboslop.render.asset_cache;
import roboslop.scene.document;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.scene.runtime;
import roboslop.scene.savegame;
import roboslop.scene.transform;

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

// A world with the two entities the brain refers to plus one scene
// object, which is all captureSave looks at.
struct Fixture {
    roboslop::World world;
    roboslop::Entity robot = world.create();
    roboslop::Entity player = world.create();
    roboslop::Entity crate = world.create();

    Fixture() {
        world.emplace<roboslop::Transform>(
            robot, roboslop::Transform{.position = {1.0F, 0.0F, 2.0F}}
        );
        world.emplace<roboslop::Transform>(
            player, roboslop::Transform{.position = {0.0F, 1.0F, 5.0F}}
        );
        world.emplace<roboslop::Transform>(
            crate, roboslop::Transform{.position = {-3.0F, 0.0F, 0.0F}}
        );
        world.emplace<roboslop::SceneIdentity>(
            crate, roboslop::SceneIdentity{.id = "crate", .name = "Crate"}
        );
    }
};

[[nodiscard]] auto brainWithMemory(const Fixture& f) -> gorden::AgentBrain {
    gorden::AgentBrain brain(
        std::make_unique<roboslop::ScriptedProvider>(std::vector<roboslop::ChatResponse>{}),
        gorden::BrainConfig{},
        f.robot,
        f.player
    );
    gorden::AgentMemory memory;
    memory.remember("the crate hides a key", 1, 10.0);
    memory.setGoal("open the crate", 11.0);
    memory.believe({.subject = "crate", .predicate = "contents", .value = "a key"});
    brain.setMemory(std::move(memory), 12.0);
    return brain;
}

} // namespace

TEST_CASE("savePath puts slots under the state directory", "[agent][save]") {
    const auto path = gorden::savePath("default");
    REQUIRE(path.filename() == "default.json");
    REQUIRE(path.parent_path().filename() == "saves");
    REQUIRE(path.parent_path().parent_path().filename() == "gorden");
}

TEST_CASE("A capture carries the scene objects and the agent's memory", "[agent][save]") {
    Fixture f;
    auto brain = brainWithMemory(f);
    const auto save =
        gorden::captureSave(f.world, brain, gorden::FirstRoomProgress{}, "scenes/room.json");

    REQUIRE(save.app.at("version") == 3);
    REQUIRE(save.scene == "scenes/room.json");
    REQUIRE(save.objects.size() == 1);
    REQUIRE(save.objects[0].id == "crate");
    REQUIRE(save.objects[0].transform.position.x == -3.0F);
    REQUIRE(save.app.at("robot").at("position")[0] == 1.0F);
    REQUIRE(save.app.at("player").at("position")[2] == 5.0F);
    REQUIRE(save.app.at("memory").at("episodes").size() == 1);
    REQUIRE(save.app.at("sim_time") == 12.0);
    REQUIRE(save.app.at("first_room").at("door_open") == false);
    REQUIRE(roboslop::validateSaveGame(save));
}

TEST_CASE("A capture carries the first room's progress", "[agent][save]") {
    Fixture f;
    auto brain = brainWithMemory(f);
    const auto bay = f.world.create();
    f.world.emplace<roboslop::Transform>(bay, roboslop::Transform{});
    f.world.emplace<roboslop::SceneIdentity>(
        bay,
        roboslop::SceneIdentity{.id = std::string{gorden::ConduitBayId}, .name = "Conduit bay C"}
    );
    f.world.emplace<gorden::Inspectable>(bay, gorden::Inspectable{.inspected = true});

    const auto save = gorden::captureSave(
        f.world,
        brain,
        gorden::FirstRoomProgress{.interlockVerified = true, .doorOpen = true},
        "scenes/room.json"
    );
    const auto& room = save.app.at("first_room");
    REQUIRE(room.at("interlock_verified") == true);
    REQUIRE(room.at("door_open") == true);
    REQUIRE(room.at("conduit_bay_inspected") == true);
}

TEST_CASE("A captured save survives a trip through a file", "[agent][save]") {
    Fixture f;
    auto brain = brainWithMemory(f);
    const auto save =
        gorden::captureSave(f.world, brain, gorden::FirstRoomProgress{}, "scenes/room.json");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = std::filesystem::temp_directory_path() /
                      ("roboslop-gorden-save-" + std::to_string(unique) + ".json");
    REQUIRE(roboslop::saveSaveGame(file, save));
    const auto reopened = roboslop::loadSaveGame(file);
    REQUIRE(reopened);
    REQUIRE(reopened->app == save.app);
    const auto memory = gorden::memoryFromJson(reopened->app.at("memory"));
    REQUIRE(memory);
    REQUIRE(memory->recall("crate key", 5).size() == 1);
    REQUIRE(memory->activeGoals().size() == 1);
    REQUIRE(memory->beliefs().size() == 1);
    std::filesystem::remove(file);
}

TEST_CASE("Rejected app payload leaves the world untouched", "[agent][save]") {
    Fixture fixture;
    auto brain = brainWithMemory(fixture);
    auto save =
        gorden::captureSave(fixture.world, brain, gorden::FirstRoomProgress{}, "scenes/room.json");
    save.app["robot"]["position"] = {8.0F, 0.0F, 0.0F};
    SECTION("unsupported app version") {
        save.app["version"] = 99;
    }
    SECTION("invalid memory") {
        save.app["memory"]["version"] = 99;
    }
    SECTION("invalid character position") {
        save.app["player"]["position"][0] = std::numeric_limits<float>::infinity();
    }
    SECTION("invalid simulation time") {
        save.app["sim_time"] = -1.0;
    }
    SECTION("exit door open without a verified interlock") {
        save.app["first_room"]["door_open"] = true;
    }
    SECTION("missing first-room progress") {
        save.app.erase("first_room");
    }
    // No render context: rejecting the payload must happen before scene
    // replacement can allocate graphics resources or destroy entities.
    roboslop::AssetCache assets{"assets"};
    roboslop::SceneRuntime runtime;
    gorden::FirstRoomProgress progress{.interlockVerified = true};
    const auto result = gorden::applySave(
        fixture.world, assets, runtime, roboslop::SceneDocument{}, brain, progress, save
    );
    REQUIRE_FALSE(result);
    REQUIRE(progress.interlockVerified);
    REQUIRE(fixture.world.valid(fixture.crate));
    REQUIRE(fixture.world.get<roboslop::Transform>(fixture.robot).position.x == 1.0F);
    REQUIRE(fixture.world.get<roboslop::Transform>(fixture.player).position.x == 0.0F);
    REQUIRE(brain.simTime() == 12.0);
    REQUIRE(brain.memory().activeGoals().size() == 1);
}
