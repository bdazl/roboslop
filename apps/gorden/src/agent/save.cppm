module;

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

export module gorden.save;

import gorden.first_room;
import gorden.player;
import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import roboslop.core.error;
import roboslop.core.paths;
import roboslop.ecs;
import roboslop.render.asset_cache;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.savegame;
import roboslop.scene.transform;

namespace gorden {

// Gorden's half of a save file. The engine stores the scene and the
// transforms of its objects (see docs/save-format.md); everything that
// is Gorden's own — the robot, the player, the robot's memory and the
// first room's progress — goes in the save's opaque `app` object,
// versioned here.
constexpr int AppPayloadVersion = 3;

[[nodiscard]] static auto conduitBayInspected(roboslop::World& world) -> bool {
    const auto bay = findSceneEntity(world, ConduitBayId);
    const auto* inspectable = bay ? world.tryGet<Inspectable>(*bay) : nullptr;
    return inspectable != nullptr && inspectable->inspected;
}

export [[nodiscard]] auto savePath(std::string_view slot) -> std::filesystem::path {
    return roboslop::stateDir() / "gorden" / "saves" / (std::string{slot} + ".json");
}

// `world` is taken by mutable reference only to dodge a clang 22 crash
// when the const forEach overload is instantiated here; nothing in this
// function writes to the world.
export [[nodiscard]] auto captureSave(
    roboslop::World& world,
    const AgentBrain& brain,
    const FirstRoomProgress& progress,
    std::string scene
) -> roboslop::SaveGame {
    roboslop::SaveGame save{.scene = std::move(scene)};
    world.forEach<roboslop::SceneIdentity, roboslop::Transform>(
        [&save](
            roboslop::Entity entity,
            const roboslop::SceneIdentity& identity,
            const roboslop::Transform& transform
        ) {
            (void)entity;
            save.objects.push_back({.id = identity.id, .transform = transform});
        }
    );
    save.app = {
        {"version", AppPayloadVersion},
        {"robot", roboslop::transformToJson(world.get<roboslop::Transform>(brain.robotEntity()))},
        {"player", roboslop::transformToJson(world.get<roboslop::Transform>(brain.playerEntity()))},
        {"sim_time", brain.simTime()},
        {"memory", toJson(brain.memory())},
        {"first_room",
         {
             {"interlock_verified", progress.interlockVerified},
             {"door_open", progress.doorOpen},
             {"conduit_bay_inspected", conduitBayInspected(world)},
         }},
    };
    return save;
}

export [[nodiscard]] auto saveError(std::string context) -> roboslop::Error {
    return {
        .category = "gorden.save",
        .code = 1,
        .message = "invalid save payload",
        .context = std::move(context)
    };
}

// Rebuilds the scene from the authored document with the saved
// transforms written over it, then restores Gorden's own state. Going
// through SceneRuntime::replace rather than writing transforms in place
// is what keeps the physics bodies consistent with where the objects
// end up.
export [[nodiscard]] auto applySave(
    roboslop::World& world,
    roboslop::AssetCache& assets,
    roboslop::SceneRuntime& runtime,
    roboslop::SceneDocument document,
    AgentBrain& brain,
    FirstRoomProgress& progress,
    const roboslop::SaveGame& save
) -> roboslop::Result<void> {
    // Validate the whole app payload before rebuilding bodies or changing
    // transforms. A rejected load must not leave a live character at the
    // old physics position with a new visual position/contact environment.
    roboslop::Transform robotTransform;
    roboslop::Transform playerTransform;
    AgentMemory restoredMemory;
    double simTime = 0.0;
    FirstRoomProgress restoredProgress;
    bool bayInspected = false;
    try {
        const int version = save.app.value("version", 0);
        if (version < 1 || version > AppPayloadVersion) {
            return std::unexpected(saveError("unsupported gorden payload version"));
        }
        robotTransform = roboslop::transformFromJson(save.app.at("robot"));
        playerTransform = roboslop::transformFromJson(save.app.at("player"));
        simTime = save.app.at("sim_time").get<double>();
        if (!roboslop::transformValid(robotTransform) ||
            !roboslop::transformValid(playerTransform) || !std::isfinite(simTime) ||
            simTime < 0.0) {
            return std::unexpected(saveError("invalid actor transform or simulation time"));
        }
        // Version 1 scaled a sphere into the placeholder avatar. The new
        // model is authored in metres; preserve position/yaw, not that scale.
        if (version == 1) {
            playerTransform.scale = glm::vec3{1.0F};
        }
        auto memory = memoryFromJson(save.app.at("memory"));
        if (!memory) {
            return std::unexpected(memory.error());
        }
        restoredMemory = std::move(*memory);
        // Saves from before the first room's puzzle start it afresh.
        if (version >= 3) {
            const auto& room = save.app.at("first_room");
            restoredProgress.interlockVerified = room.at("interlock_verified").get<bool>();
            restoredProgress.doorOpen = room.at("door_open").get<bool>();
            bayInspected = room.at("conduit_bay_inspected").get<bool>();
            if (restoredProgress.doorOpen && !restoredProgress.interlockVerified) {
                return std::unexpected(saveError("exit door open without a verified interlock"));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(saveError(e.what()));
    }

    for (const auto& saved : save.objects) {
        // The door's pose follows the progress restored below, from the
        // authored closed door; its saved transform is not trusted.
        if (saved.id == ExitDoorId) {
            continue;
        }
        auto it = std::ranges::find(document.objects, saved.id, &roboslop::SceneObject::id);
        if (it == document.objects.end()) {
            // The scene was edited since the save was written; the
            // object is simply gone, which is not an error.
            continue;
        }
        it->transform = saved.transform;
    }
    if (auto replaced = runtime.replace(world, assets, document, true); !replaced) {
        return replaced;
    }
    // replace() destroyed the old scene entities, so the new ones need
    // their perception names and semantics, exactly as after the app's
    // first instantiation.
    attachSceneSemantics(world);
    if (const auto bay = findSceneEntity(world, ConduitBayId)) {
        if (auto* inspectable = world.tryGet<Inspectable>(*bay)) {
            inspectable->inspected = bayInspected;
        }
    }
    progress = restoredProgress;
    applyFirstRoomState(world, progress);

    world.get<roboslop::Transform>(brain.robotEntity()) = robotTransform;
    world.get<roboslop::Transform>(brain.playerEntity()) = playerTransform;
    brain.setMemory(std::move(restoredMemory), simTime);

    if (auto* player = world.tryGet<Player>(brain.playerEntity())) {
        player->reset();
    }

    // A move from before the load must not resume towards a target the
    // robot no longer has a reason to walk to.
    if (auto* motion = world.tryGet<RobotMotion>(brain.robotEntity()); motion != nullptr) {
        motion->target.reset();
        motion->arrived = false;
    }
    return {};
}

} // namespace gorden
