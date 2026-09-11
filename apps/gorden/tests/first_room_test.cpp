import gorden.agent.brain;
import gorden.agent.observation;
import gorden.first_room;
import gorden.player;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.scene.runtime;
import roboslop.scene.transform;
import roboslop.sched;
import roboslop.shell;
import roboslop.vfs;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr float Dt = 1.0F / 60.0F;

// A floor, the exit door as authored in room.json, and the first
// room's gameplay state in the world's context, as the app sets it up.
struct Room {
    roboslop::JoltWorld physics = roboslop::JoltWorld::make();
    roboslop::World world;
    gorden::AgentBrain* brain = nullptr;

    Room() {
        roboslop::installJoltWorld(world, physics);
        box("floor", {0.0F, -0.25F, 0.0F}, {6.0F, 0.25F, 6.0F});
        box(gorden::ExitDoorId, {5.8F, 1.6F, 0.0F}, {0.225F, 1.6F, 1.2F});
        gorden::attachSceneSemantics(world);

        const auto robot = world.create();
        world.emplace<roboslop::Transform>(robot, roboslop::Transform{});
        auto& ctx = world.registry().ctx();
        ctx.emplace<gorden::FirstRoomProgress>();
        brain = &ctx.emplace<gorden::AgentBrain>(
            std::make_unique<roboslop::ScriptedProvider>(), gorden::BrainConfig{}, robot, robot
        );
    }

    auto box(std::string_view id, glm::vec3 position, glm::vec3 halfExtents) -> void {
        const auto e = world.create();
        world.emplace<roboslop::SceneIdentity>(
            e, roboslop::SceneIdentity{.id = std::string{id}, .name = std::string{id}}
        );
        world.emplace<roboslop::Transform>(e, roboslop::Transform{.position = position});
        world.emplace<roboslop::BodyDesc>(
            e,
            roboslop::BodyDesc{
                .shape = roboslop::BoxShape{.halfExtents = halfExtents},
                .motion = roboslop::BodyMotion::Static
            }
        );
        roboslop::SystemCtx context{.world = &world, .dt = static_cast<double>(Dt)};
        roboslop::physicsSpawn(context);
    }

    auto door() -> roboslop::Entity {
        return *gorden::findSceneEntity(world, gorden::ExitDoorId);
    }

    auto progress() -> gorden::FirstRoomProgress& {
        return world.registry().ctx().get<gorden::FirstRoomProgress>();
    }

    // Walks a player towards +X through the doorway for two seconds and
    // returns where it ends up.
    auto walkThroughDoorway() -> float {
        gorden::Player player;
        roboslop::Transform transform{.position = {4.5F, 0.9F, 0.0F}};
        for (int i = 0; i < 120; ++i) {
            physics.step(Dt);
            gorden::movePlayer(player, transform, physics, {3.0F, 0.0F, 0.0F}, Dt);
        }
        return transform.position.x;
    }
};

using Words = std::vector<std::string>;

} // namespace

TEST_CASE("Only the authored tag and relay order verify the interlock", "[first_room]") {
    gorden::FirstRoomProgress progress;
    REQUIRE(
        gorden::verifyInterlock(progress, Words{"C-17", "blue", "yellow", "red", "blue"}) ==
        gorden::Verification::Rejected
    );
    REQUIRE(
        gorden::verifyInterlock(progress, Words{"C-17", "blue", "yellow", "blue"}) ==
        gorden::Verification::Rejected
    );
    REQUIRE_FALSE(progress.interlockVerified);

    REQUIRE(
        gorden::verifyInterlock(progress, Words{"c-17", "Blue,", "YELLOW,", "blue,", "red"}) ==
        gorden::Verification::Accepted
    );
    REQUIRE(progress.interlockVerified);
    REQUIRE(
        gorden::verifyInterlock(progress, Words{"C-17", "blue", "yellow", "blue", "red"}) ==
        gorden::Verification::AlreadyVerified
    );
}

TEST_CASE("The exit door blocks until the verified interlock opens it", "[first_room]") {
    Room room;
    REQUIRE(room.walkThroughDoorway() < 5.6F);
    REQUIRE(room.world.get<gorden::Inspectable>(room.door()).state == "locked");

    REQUIRE_FALSE(gorden::openExitDoor(room.world, room.progress(), *room.brain));
    REQUIRE(room.world.has<roboslop::RigidBody>(room.door()));
    REQUIRE(room.brain->pendingEventCount() == 0);

    REQUIRE(
        gorden::verifyInterlock(room.progress(), Words{"C-17", "blue", "yellow", "blue", "red"}) ==
        gorden::Verification::Accepted
    );
    REQUIRE(gorden::openExitDoor(room.world, room.progress(), *room.brain));
    REQUIRE(room.progress().doorOpen);
    REQUIRE_FALSE(room.world.has<roboslop::RigidBody>(room.door()));
    REQUIRE(room.world.get<roboslop::Transform>(room.door()).position.z == Catch::Approx(-2.4F));
    REQUIRE(room.world.get<gorden::Inspectable>(room.door()).state == "open");
    REQUIRE(room.brain->pendingEventCount() == 1);
    REQUIRE(std::ranges::any_of(room.brain->actionLog(), [](const std::string& line) {
        return line.contains("world_event: The exit door slid open.");
    }));
    REQUIRE(room.walkThroughDoorway() > 6.2F);

    // A second open, or re-applying the state, leaves the door where it is.
    REQUIRE_FALSE(gorden::openExitDoor(room.world, room.progress(), *room.brain));
    gorden::applyFirstRoomState(room.world, room.progress());
    REQUIRE(room.world.get<roboslop::Transform>(room.door()).position.z == Catch::Approx(-2.4F));
}

TEST_CASE("The terminal runs the interlock puzzle", "[first_room]") {
    Room room;
    roboslop::Vfs fs;
    roboslop::Shell shell(fs, {.user = "anna", .host = "gorden", .home = "/"});
    gorden::registerFirstRoomCommands(shell, fs, room.world);

    const auto status = shell.execute("door status");
    REQUIRE(status.status == 0);
    REQUIRE(status.output.starts_with("LOCKED\n"));
    REQUIRE(status.output.contains("maintenance tag     UNKNOWN"));

    REQUIRE(shell.execute("cat /var/log/interlock.log").output.contains("conduit bay C"));
    REQUIRE(shell.execute("door open").status == 1);
    REQUIRE(shell.execute("door").status == 1);
    REQUIRE(shell.execute("interlock verify C-17 red").status == 1);

    const auto verified = shell.execute("interlock verify C-17 blue yellow blue red");
    REQUIRE(verified.status == 0);
    REQUIRE(verified.output == "Verification accepted.\n");
    REQUIRE(shell.execute("door status").output.starts_with("UNLOCKED\n"));

    const auto opened = shell.execute("door open");
    REQUIRE(opened.status == 0);
    REQUIRE(opened.output == "Opening exit...\n");
    REQUIRE(room.progress().doorOpen);
    REQUIRE(shell.execute("door status").output.starts_with("OPEN\n"));
}
