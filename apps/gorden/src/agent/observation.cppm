module;

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

export module gorden.agent.observation;

import roboslop.ecs;
import roboslop.scene.transform;

namespace gorden {

// Human-readable identity the robot perceives entities by. Names are
// what the model sees and what tools refer to; entity ids never reach
// the model.
export struct Named {
    std::string name{};
};

// An active goal as the robot sees it in an observation. Goals live in
// gorden.agent.memory; they reach the observation so that validation
// and the model see the same list.
export struct ObservedGoal {
    std::string id;
    std::string text{};
};

// What an entity shows the robot beyond its name. `state` is visible
// from anywhere in range; `detail` only from within inspect reach (see
// Rules::inspectReach), which is what makes the robot go and look.
export struct Inspectable {
    std::string state{};
    std::string detail{};
    bool inspected = false; // the robot has read the detail
};

export struct ObservedEntity {
    std::string name{};
    glm::vec3 position{0.0F};
    float distance = 0.0F;
    std::string state{};
    // For the simulation side only; never serialized for the model.
    roboslop::Entity entity{};
};

// What the robot gets to know for one think. Deliberately small: the
// tools need positions, names and the visible state of things (the
// "semantic-first, minimal" direction in docs/architecture.md).
export struct Observation {
    glm::vec3 robotPosition{0.0F};
    glm::vec3 playerPosition{0.0F};
    bool robotMoving = false;
    std::vector<ObservedEntity> nearby{}; // sorted by distance, robot excluded
    std::vector<std::string> recentEvents{};
    // Filled by the brain from memory, not by buildObservation.
    std::vector<ObservedGoal> goals{};
    std::vector<std::string> beliefs{}; // one rendered line each
    std::string playerMessage{};
};

// Engine-side truth → robot perception. Every Named entity with a
// Transform within `radius` of the robot is visible; there is no
// line-of-sight yet.
export [[nodiscard]] auto buildObservation(
    const roboslop::World& world, roboslop::Entity robot, roboslop::Entity player, float radius
) -> Observation {
    Observation obs;
    obs.robotPosition = world.get<roboslop::Transform>(robot).position;
    obs.playerPosition = world.get<roboslop::Transform>(player).position;

    world.forEach<Named, roboslop::Transform>(
        [&](roboslop::Entity e, const Named& named, const roboslop::Transform& t) {
            if (e == robot) {
                return;
            }
            const float d = glm::distance(t.position, obs.robotPosition);
            if (d <= radius) {
                obs.nearby.push_back(
                    ObservedEntity{
                        .name = named.name,
                        .position = t.position,
                        .distance = d,
                        .state = world.has<Inspectable>(e) ? world.get<Inspectable>(e).state
                                                           : std::string{},
                        .entity = e,
                    }
                );
            }
        }
    );
    std::ranges::sort(obs.nearby, [](const ObservedEntity& a, const ObservedEntity& b) {
        return a.distance < b.distance;
    });
    return obs;
}

[[nodiscard]] static auto vec3Json(const glm::vec3& v) -> nlohmann::json {
    // Two decimals: enough for a 10 m yard, fewer tokens for the model.
    auto r = [](float f) { return static_cast<double>(static_cast<int>(f * 100.0F)) / 100.0; };
    return {{"x", r(v.x)}, {"y", r(v.y)}, {"z", r(v.z)}};
}

// The text the model actually reads. Kept as one JSON object so the
// prompt format is stable and easy to log.
export [[nodiscard]] auto observationToJson(const Observation& obs) -> std::string {
    nlohmann::json j;
    j["robot"] = {{"position", vec3Json(obs.robotPosition)}, {"moving", obs.robotMoving}};
    j["player"] = {{"position", vec3Json(obs.playerPosition)}};
    nlohmann::json nearby = nlohmann::json::array();
    for (const auto& e : obs.nearby) {
        nlohmann::json entry{
            {"name", e.name},
            {"position", vec3Json(e.position)},
            {"distance", static_cast<double>(static_cast<int>(e.distance * 10.0F)) / 10.0},
        };
        if (!e.state.empty()) {
            entry["state"] = e.state;
        }
        nearby.push_back(std::move(entry));
    }
    j["nearby"] = std::move(nearby);
    j["events"] = obs.recentEvents;
    if (!obs.goals.empty()) {
        nlohmann::json goals = nlohmann::json::array();
        for (const auto& g : obs.goals) {
            goals.push_back({{"id", g.id}, {"text", g.text}});
        }
        j["goals"] = std::move(goals);
    }
    if (!obs.beliefs.empty()) {
        j["beliefs"] = obs.beliefs;
    }
    if (!obs.playerMessage.empty()) {
        j["player_message"] = obs.playerMessage;
    }
    return j.dump();
}

} // namespace gorden
