module;

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module gorden.first_room;

import gorden.agent.brain;
import gorden.agent.observation;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.scene.runtime;
import roboslop.scene.transform;
import roboslop.shell;
import roboslop.vfs;

namespace gorden {

// Scene ids of the first room's gameplay objects, as authored in
// assets/scenes/room.json.
export constexpr std::string_view ExitDoorId = "exit-door";
export constexpr std::string_view ConduitBayId = "conduit-bay-c";

// The open door slides this far along -Z, into the exit wall's back
// section, which is exactly one door width.
export constexpr glm::vec3 DoorOpenOffset{0.0F, 0.0F, -2.4F};

// The first room's progression. The door's pose and collision follow
// `doorOpen`; the scene document always holds the closed door.
export struct FirstRoomProgress {
    bool interlockVerified = false;
    bool doorOpen = false;
};

namespace {

struct Semantics {
    std::string_view id;
    std::string_view state;
    std::string_view detail;
};

// What the first room's objects show the robot. Kept here rather than
// in the scene document: it is Gorden's gameplay, not engine data.
constexpr std::array RoomSemantics{
    Semantics{
        .id = ExitDoorId,
        .state = "locked",
        .detail = "A heavy sliding door held shut by a safety interlock.",
    },
    Semantics{
        .id = "power-unit",
        .state = "humming",
        .detail = "Cables run from its back into a conduit bay on the wall behind it.",
    },
    Semantics{
        .id = ConduitBayId,
        .state = "",
        .detail = "Maintenance tag C-17. Relays, left to right: blue, yellow, blue, red.",
    },
    Semantics{
        .id = "terminal-monitor",
        .state = "online",
        .detail = "The computer that controls the exit door.",
    },
};

// The authored answer, lower case: tag first, then the relay order.
constexpr std::array<std::string_view, 5> InterlockAnswer{"c-17", "blue", "yellow", "blue", "red"};

constexpr std::string_view InterlockLog =
    "[controller] replacement controller installed; door interlock reset\n"
    "[interlock]  actuator power OK\n"
    "[interlock]  awaiting verification: maintenance tag, conduit order\n"
    "[maint]      After replacing the controller, verify the maintenance tag in\n"
    "[maint]      conduit bay C and enter the attached relay order:\n"
    "[maint]        interlock verify <tag> <relay colours, left to right>\n";

[[nodiscard]] auto error(std::string_view message) -> roboslop::Error {
    return {.category = "gorden.first_room", .code = 1, .message = message, .context = {}};
}

[[nodiscard]] auto normalized(std::string_view word) -> std::string {
    std::string out;
    for (const char c : word) {
        if (c != ',') {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

} // namespace

export [[nodiscard]] auto findSceneEntity(roboslop::World& world, std::string_view id)
    -> std::optional<roboslop::Entity> {
    std::optional<roboslop::Entity> found;
    world.forEach<roboslop::SceneIdentity>([&](auto entity, const auto& identity) {
        if (identity.id == id) {
            found = entity;
        }
    });
    return found;
}

// Gives every scene entity the perception name the observation looks
// for, and the first room's objects their semantic state. Needed after
// each SceneRuntime::replace, which recreates the scene entities.
export auto attachSceneSemantics(roboslop::World& world) -> void {
    world.forEach<roboslop::SceneIdentity>([&world](auto entity, const auto& identity) {
        world.emplace<Named>(entity, Named{.name = identity.name});
        for (const auto& s : RoomSemantics) {
            if (s.id == identity.id) {
                world.emplace<Inspectable>(
                    entity,
                    Inspectable{.state = std::string{s.state}, .detail = std::string{s.detail}}
                );
            }
        }
    });
}

// Brings the door in line with `progress`: open means slid into the
// wall, without collision. A door without a body has already been
// opened, so calling this twice does not slide it further. Call
// between fixed steps.
export auto applyFirstRoomState(roboslop::World& world, const FirstRoomProgress& progress) -> void {
    if (!progress.doorOpen) {
        return;
    }
    const auto door = findSceneEntity(world, ExitDoorId);
    if (!door ||
        (!world.has<roboslop::RigidBody>(*door) && !world.has<roboslop::BodyDesc>(*door))) {
        return;
    }
    roboslop::releasePhysicsBody(world, *door);
    world.get<roboslop::Transform>(*door).position += DoorOpenOffset;
    if (auto* inspectable = world.tryGet<Inspectable>(*door)) {
        inspectable->state = "open";
    }
}

export enum class Verification { Accepted, AlreadyVerified, Rejected };

// The answer is the gate: it is accepted whether or not the robot has
// read the tag, and a wrong answer costs nothing but another try.
export auto verifyInterlock(FirstRoomProgress& progress, std::span<const std::string> answer)
    -> Verification {
    const bool correct = std::ranges::equal(answer, InterlockAnswer, {}, normalized);
    if (!correct) {
        return Verification::Rejected;
    }
    if (progress.interlockVerified) {
        return Verification::AlreadyVerified;
    }
    progress.interlockVerified = true;
    return Verification::Accepted;
}

// The one way the door opens. The robot perceives it as a world event.
export [[nodiscard]] auto
openExitDoor(roboslop::World& world, FirstRoomProgress& progress, AgentBrain& brain)
    -> roboslop::Result<void> {
    if (!progress.interlockVerified) {
        return std::unexpected(error("interlock verification incomplete"));
    }
    if (progress.doorOpen) {
        return std::unexpected(error("exit already open"));
    }
    if (!findSceneEntity(world, ExitDoorId)) {
        return std::unexpected(error("no exit door in this scene"));
    }
    progress.doorOpen = true;
    applyFirstRoomState(world, progress);
    brain.perceive("The exit door slid open.");
    return {};
}

export [[nodiscard]] auto doorStatus(const FirstRoomProgress& progress) -> std::string {
    const std::string_view check = progress.interlockVerified ? "VERIFIED" : "UNKNOWN";
    std::string out;
    if (progress.doorOpen) {
        out = "OPEN\n";
    } else {
        out = progress.interlockVerified ? "UNLOCKED\n" : "LOCKED\n";
    }
    out += progress.interlockVerified ? "Interlock verified:\n"
                                      : "Interlock verification incomplete:\n";
    out += std::format(
        "  actuator power      OK\n"
        "  maintenance tag     {0}\n"
        "  conduit order       {0}\n",
        check
    );
    if (!progress.interlockVerified) {
        out += "See /var/log/interlock.log.\n";
    }
    return out;
}

// The computer's side of the puzzle: thin commands over the operations
// above, plus the log that says where to look. Progress and the brain
// are read from the world's context on each call.
export auto
registerFirstRoomCommands(roboslop::Shell& shell, roboslop::Vfs& fs, roboslop::World& world)
    -> void {
    (void)fs.mountLive(
        "/var/log/interlock.log",
        roboslop::LiveFile{.read = [] { return std::string{InterlockLog}; }, .write = {}}
    );
    auto* w = &world;
    shell.registerCommand(
        "door",
        "status|open - the exit door",
        [w](roboslop::CommandContext& c) -> roboslop::ShellResult {
            auto& ctx = w->registry().ctx();
            auto& progress = ctx.get<FirstRoomProgress>();
            if (c.args.size() == 2 && c.args[1] == "status") {
                return {.output = doorStatus(progress)};
            }
            if (c.args.size() == 2 && c.args[1] == "open") {
                if (auto opened = openExitDoor(*w, progress, ctx.get<AgentBrain>()); !opened) {
                    return {
                        .output = "door: " + std::string{opened.error().message} + "\n", .status = 1
                    };
                }
                return {.output = "Opening exit...\n"};
            }
            return {.output = "usage: door status|open\n", .status = 1};
        }
    );
    shell.registerCommand(
        "interlock",
        "verify <tag> <colour>... - verify the door interlock",
        [w](roboslop::CommandContext& c) -> roboslop::ShellResult {
            if (c.args.size() < 3 || c.args[1] != "verify") {
                return {.output = "usage: interlock verify <tag> <relay colour>...\n", .status = 1};
            }
            auto& progress = w->registry().ctx().get<FirstRoomProgress>();
            switch (verifyInterlock(progress, std::span{c.args}.subspan(2))) {
            case Verification::Accepted:
                return {.output = "Verification accepted.\n"};
            case Verification::AlreadyVerified:
                return {.output = "Interlock already verified.\n"};
            case Verification::Rejected:
                break;
            }
            return {.output = "Verification rejected.\n", .status = 1};
        }
    );
}

} // namespace gorden
