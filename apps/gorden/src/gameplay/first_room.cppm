module;

#include <array>
#include <string>
#include <string_view>

export module gorden.first_room;

import gorden.agent.observation;
import roboslop.ecs;
import roboslop.scene.runtime;

namespace gorden {

// Scene ids of the first room's gameplay objects, as authored in
// assets/scenes/room.json.
export constexpr std::string_view ExitDoorId = "exit-door";
export constexpr std::string_view ConduitBayId = "conduit-bay-c";

namespace detail {

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

} // namespace detail

// Gives every scene entity the perception name the observation looks
// for, and the first room's objects their semantic state. Needed after
// each SceneRuntime::replace, which recreates the scene entities.
export auto attachSceneSemantics(roboslop::World& world) -> void {
    world.forEach<roboslop::SceneIdentity>([&world](auto entity, const auto& identity) {
        world.emplace<Named>(entity, Named{.name = identity.name});
        for (const auto& s : detail::RoomSemantics) {
            if (s.id == identity.id) {
                world.emplace<Inspectable>(
                    entity,
                    Inspectable{.state = std::string{s.state}, .detail = std::string{s.detail}}
                );
            }
        }
    });
}

} // namespace gorden
