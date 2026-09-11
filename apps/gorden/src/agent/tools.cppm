module;

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module gorden.agent.tools;

import gorden.agent.memory;
import gorden.agent.observation;
import roboslop.core.error;
import roboslop.llm;

namespace gorden {

// The three high-level actions of the first slice. A ToolCall from the
// model is only a *proposal*; parseToolCall turns its JSON into one of
// these, and validate() decides whether the simulation will honour it.
export struct MoveTo {
    glm::vec3 target{0.0F};
};

export struct Inspect {
    std::string name{};
};

export struct Say {
    std::string text{};
};

// The memory actions. The robot decides what is worth remembering, so
// every write to gorden.agent.memory comes through one of these rather
// than from the simulation behind its back.
export struct Remember {
    std::string text{};
};

export struct Recall {
    std::string query{};
    std::size_t limit = 3;
};

export struct Believe {
    std::string subject{};
    std::string predicate{};
    std::string value{};
    std::string source{};
    float confidence = 0.5F;
};

export struct SetGoal {
    std::string text{};
};

export struct CloseGoal {
    std::string id{};
    GoalStatus status = GoalStatus::Done;
};

export using Command =
    std::variant<MoveTo, Inspect, Say, Remember, Recall, Believe, SetGoal, CloseGoal>;

export enum class ToolError : int {
    UnknownTool = 1,
    BadArguments = 2,
    TooFar = 3,
    OutOfWorld = 4,
    UnknownEntity = 5,
    EmptyText = 6,
    TextTooLong = 7,
    UnknownGoal = 8,
    BadConfidence = 9,
    TooManyGoals = 10,
};

export [[nodiscard]] auto toError(ToolError e, std::string ctx = {}) -> roboslop::Error {
    const auto make = [&](std::string_view msg) {
        return roboslop::Error{
            .category = "gorden.agent.tools",
            .code = static_cast<int>(e),
            .message = msg,
            .context = std::move(ctx)
        };
    };
    switch (e) {
    case ToolError::UnknownTool:
        return make("unknown tool");
    case ToolError::BadArguments:
        return make("tool arguments are malformed");
    case ToolError::TooFar:
        return make("target is farther than the robot may travel in one move");
    case ToolError::OutOfWorld:
        return make("target is outside the world bounds");
    case ToolError::UnknownEntity:
        return make("no visible entity with that name");
    case ToolError::EmptyText:
        return make("say text is empty");
    case ToolError::TextTooLong:
        return make("text is too long");
    case ToolError::UnknownGoal:
        return make("no active goal with that id");
    case ToolError::BadConfidence:
        return make("confidence must be between 0 and 1");
    case ToolError::TooManyGoals:
        return make("too many goals are already active");
    }
    return make("unknown ToolError");
}

// Tool schemas as sent to the model. Names here are the contract with
// parseToolCall below.
export [[nodiscard]] auto toolSpecs() -> std::vector<roboslop::ToolSpec> {
    return {
        roboslop::ToolSpec{
            .name = "moveTo",
            .description = "Walk to a point on the ground. Coordinates are world metres; "
                           "y is up, so give x and z.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "x":{"type":"number"},"z":{"type":"number"}},
                "required":["x","z"]})",
        },
        roboslop::ToolSpec{
            .name = "inspect",
            .description = "Look at a visible entity by name: where it is and its state. "
                           "Details such as labels are only readable within 1 m, so move "
                           "next to it first.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "name":{"type":"string"}},"required":["name"]})",
        },
        roboslop::ToolSpec{
            .name = "say",
            .description = "Say something out loud to the player. Keep it short.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "text":{"type":"string"}},"required":["text"]})",
        },
        roboslop::ToolSpec{
            .name = "remember",
            .description = "Store one thing worth remembering later. Write it as a full "
                           "sentence with the words you would search for.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "text":{"type":"string"}},"required":["text"]})",
        },
        roboslop::ToolSpec{
            .name = "recall",
            .description = "Search your memories by keywords and get the best matches back.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "query":{"type":"string"},"limit":{"type":"integer"}},
                "required":["query"]})",
        },
        roboslop::ToolSpec{
            .name = "believe",
            .description = "Record what you hold true about something, and where you learned "
                           "it. A new value replaces the one you held before.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "subject":{"type":"string"},"predicate":{"type":"string"},
                "value":{"type":"string"},"source":{"type":"string"},
                "confidence":{"type":"number"}},
                "required":["subject","predicate","value"]})",
        },
        roboslop::ToolSpec{
            .name = "setGoal",
            .description = "Start pursuing an intention. Active goals are listed in every "
                           "observation.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "text":{"type":"string"}},"required":["text"]})",
        },
        roboslop::ToolSpec{
            .name = "closeGoal",
            .description = "Finish a goal by its id, as done or abandoned.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "id":{"type":"string"},
                "status":{"type":"string","enum":["done","abandoned"]}},
                "required":["id"]})",
        },
    };
}

export [[nodiscard]] auto parseToolCall(const roboslop::ToolCall& call)
    -> roboslop::Result<Command> {
    const auto args =
        nlohmann::json::parse(call.argumentsJson, nullptr, /*allow_exceptions=*/false);
    if (args.is_discarded() || !args.is_object()) {
        return std::unexpected(toError(ToolError::BadArguments, call.argumentsJson));
    }
    if (call.name == "moveTo") {
        if (!args.contains("x") || !args.contains("z") || !args["x"].is_number() ||
            !args["z"].is_number()) {
            return std::unexpected(toError(ToolError::BadArguments, "moveTo needs numeric x, z"));
        }
        return Command{MoveTo{.target = {args["x"].get<float>(), 0.0F, args["z"].get<float>()}}};
    }
    if (call.name == "inspect") {
        if (!args.contains("name") || !args["name"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "inspect needs a name"));
        }
        return Command{Inspect{.name = args["name"].get<std::string>()}};
    }
    if (call.name == "say") {
        if (!args.contains("text") || !args["text"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "say needs text"));
        }
        return Command{Say{.text = args["text"].get<std::string>()}};
    }
    if (call.name == "remember") {
        if (!args.contains("text") || !args["text"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "remember needs text"));
        }
        return Command{Remember{.text = args["text"].get<std::string>()}};
    }
    if (call.name == "recall") {
        if (!args.contains("query") || !args["query"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "recall needs a query"));
        }
        Recall recall{.query = args["query"].get<std::string>()};
        if (args.contains("limit")) {
            if (!args["limit"].is_number_unsigned()) {
                return std::unexpected(
                    toError(ToolError::BadArguments, "recall limit must be a positive number")
                );
            }
            recall.limit = args["limit"].get<std::size_t>();
        }
        return Command{recall};
    }
    if (call.name == "believe") {
        if (!args.contains("subject") || !args["subject"].is_string() ||
            !args.contains("predicate") || !args["predicate"].is_string() ||
            !args.contains("value") || !args["value"].is_string()) {
            return std::unexpected(
                toError(ToolError::BadArguments, "believe needs subject, predicate and value")
            );
        }
        Believe belief{
            .subject = args["subject"].get<std::string>(),
            .predicate = args["predicate"].get<std::string>(),
            .value = args["value"].get<std::string>(),
        };
        if (args.contains("source")) {
            if (!args["source"].is_string()) {
                return std::unexpected(
                    toError(ToolError::BadArguments, "believe source must be a string")
                );
            }
            belief.source = args["source"].get<std::string>();
        }
        if (args.contains("confidence")) {
            if (!args["confidence"].is_number()) {
                return std::unexpected(
                    toError(ToolError::BadArguments, "believe confidence must be a number")
                );
            }
            belief.confidence = args["confidence"].get<float>();
        }
        return Command{belief};
    }
    if (call.name == "setGoal") {
        if (!args.contains("text") || !args["text"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "setGoal needs text"));
        }
        return Command{SetGoal{.text = args["text"].get<std::string>()}};
    }
    if (call.name == "closeGoal") {
        if (!args.contains("id") || !args["id"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "closeGoal needs an id"));
        }
        CloseGoal close{.id = args["id"].get<std::string>()};
        if (args.contains("status")) {
            if (!args["status"].is_string()) {
                return std::unexpected(
                    toError(ToolError::BadArguments, "closeGoal status must be a string")
                );
            }
            close.status = goalStatusFromName(args["status"].get<std::string>());
        }
        return Command{close};
    }
    return std::unexpected(toError(ToolError::UnknownTool, call.name));
}

export struct Rules {
    float maxMoveDistance = 30.0F; // per moveTo, in metres from the robot
    float worldHalfExtent = 12.0F; // the ground plane is 20 m; keep a margin
    std::size_t maxSayLength = 200;
    std::size_t maxMemoryTextLength = 300; // remember, believe and setGoal texts
    std::size_t maxRecallResults = 5;      // a recall limit above this is clamped
    std::size_t maxActiveGoals = 5;
    // Horizontal distance within which inspect reads an entity's
    // details. Far enough to read conduit bay C from beside the power
    // unit, too short to read it through the cabinet from the front.
    float inspectReach = 1.0F;
};

// The validation boundary: a Command comes out only if the simulation
// will actually do it. Rejections carry the rule in Error::message so
// the model can be told why.
export [[nodiscard]] auto validate(const Command& cmd, const Observation& obs, const Rules& rules)
    -> roboslop::Result<Command> {
    // The memory tools all carry free text under the same two rules.
    const auto checkText = [&](const std::string& text,
                               std::string_view what) -> std::optional<roboslop::Error> {
        if (text.empty()) {
            return toError(ToolError::EmptyText, std::string{what});
        }
        if (text.size() > rules.maxMemoryTextLength) {
            return toError(
                ToolError::TextTooLong,
                std::format("{}: {} chars, max {}", what, text.size(), rules.maxMemoryTextLength)
            );
        }
        return std::nullopt;
    };
    return std::visit(
        [&](const auto& c) -> roboslop::Result<Command> {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveTo>) {
                if (!std::isfinite(c.target.x) || !std::isfinite(c.target.z)) {
                    return std::unexpected(toError(ToolError::BadArguments, "non-finite target"));
                }
                if (std::abs(c.target.x) > rules.worldHalfExtent ||
                    std::abs(c.target.z) > rules.worldHalfExtent) {
                    return std::unexpected(toError(
                        ToolError::OutOfWorld,
                        std::format("bounds are ±{:.0f} m", rules.worldHalfExtent)
                    ));
                }
                const glm::vec3 flatRobot{obs.robotPosition.x, 0.0F, obs.robotPosition.z};
                const float d = glm::distance(flatRobot, c.target);
                if (d > rules.maxMoveDistance) {
                    return std::unexpected(toError(
                        ToolError::TooFar,
                        std::format("{:.1f} m requested, max {:.0f} m", d, rules.maxMoveDistance)
                    ));
                }
                return Command{c};
            } else if constexpr (std::is_same_v<T, Inspect>) {
                const bool visible = std::ranges::any_of(obs.nearby, [&](const ObservedEntity& e) {
                    return e.name == c.name;
                });
                if (!visible) {
                    return std::unexpected(toError(ToolError::UnknownEntity, c.name));
                }
                return Command{c};
            } else if constexpr (std::is_same_v<T, Say>) {
                if (c.text.empty()) {
                    return std::unexpected(toError(ToolError::EmptyText));
                }
                if (c.text.size() > rules.maxSayLength) {
                    return std::unexpected(toError(
                        ToolError::TextTooLong,
                        std::format("{} chars, max {}", c.text.size(), rules.maxSayLength)
                    ));
                }
                return Command{c};
            } else if constexpr (std::is_same_v<T, Remember>) {
                if (auto bad = checkText(c.text, "remember text"); bad) {
                    return std::unexpected(*bad);
                }
                return Command{c};
            } else if constexpr (std::is_same_v<T, Recall>) {
                if (auto bad = checkText(c.query, "recall query"); bad) {
                    return std::unexpected(*bad);
                }
                // A greedy limit is clamped rather than rejected: the
                // model asked a reasonable question with a bad number.
                return Command{
                    Recall{.query = c.query, .limit = std::min(c.limit, rules.maxRecallResults)}
                };
            } else if constexpr (std::is_same_v<T, Believe>) {
                for (const auto& [text, what] :
                     {std::pair{c.subject, "belief subject"},
                      std::pair{c.predicate, "belief predicate"},
                      std::pair{c.value, "belief value"}}) {
                    if (auto bad = checkText(text, what); bad) {
                        return std::unexpected(*bad);
                    }
                }
                if (!std::isfinite(c.confidence) || c.confidence < 0.0F || c.confidence > 1.0F) {
                    return std::unexpected(
                        toError(ToolError::BadConfidence, std::format("{:.2f} given", c.confidence))
                    );
                }
                return Command{c};
            } else if constexpr (std::is_same_v<T, SetGoal>) {
                if (auto bad = checkText(c.text, "goal text"); bad) {
                    return std::unexpected(*bad);
                }
                if (obs.goals.size() >= rules.maxActiveGoals) {
                    return std::unexpected(toError(
                        ToolError::TooManyGoals,
                        std::format("max {}; close one first", rules.maxActiveGoals)
                    ));
                }
                return Command{c};
            } else {
                const bool known = std::ranges::any_of(obs.goals, [&](const ObservedGoal& g) {
                    return g.id == c.id;
                });
                if (!known) {
                    return std::unexpected(toError(ToolError::UnknownGoal, c.id));
                }
                return Command{c};
            }
        },
        cmd
    );
}

export [[nodiscard]] auto commandName(const Command& cmd) noexcept -> std::string_view {
    return std::visit(
        [](const auto& c) -> std::string_view {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveTo>) {
                return "moveTo";
            } else if constexpr (std::is_same_v<T, Inspect>) {
                return "inspect";
            } else if constexpr (std::is_same_v<T, Say>) {
                return "say";
            } else if constexpr (std::is_same_v<T, Remember>) {
                return "remember";
            } else if constexpr (std::is_same_v<T, Recall>) {
                return "recall";
            } else if constexpr (std::is_same_v<T, Believe>) {
                return "believe";
            } else if constexpr (std::is_same_v<T, SetGoal>) {
                return "setGoal";
            } else {
                return "closeGoal";
            }
        },
        cmd
    );
}

// One-line rendering for the action log.
export [[nodiscard]] auto describeCommand(const Command& cmd) -> std::string {
    return std::visit(
        [](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveTo>) {
                return std::format("moveTo({:.1f}, {:.1f})", c.target.x, c.target.z);
            } else if constexpr (std::is_same_v<T, Inspect>) {
                return std::format("inspect({})", c.name);
            } else if constexpr (std::is_same_v<T, Say>) {
                return std::format("say(\"{}\")", c.text);
            } else if constexpr (std::is_same_v<T, Remember>) {
                return std::format("remember(\"{}\")", c.text);
            } else if constexpr (std::is_same_v<T, Recall>) {
                return std::format("recall(\"{}\", {})", c.query, c.limit);
            } else if constexpr (std::is_same_v<T, Believe>) {
                return std::format(
                    "believe({} {} = {} [{}], {:.2f})",
                    c.subject,
                    c.predicate,
                    c.value,
                    c.source.empty() ? "no source" : c.source,
                    c.confidence
                );
            } else if constexpr (std::is_same_v<T, SetGoal>) {
                return std::format("setGoal(\"{}\")", c.text);
            } else {
                return std::format("closeGoal({}, {})", c.id, goalStatusName(c.status));
            }
        },
        cmd
    );
}

} // namespace gorden
