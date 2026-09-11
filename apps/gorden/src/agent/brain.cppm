module;

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module gorden.agent.brain;

import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import gorden.agent.tools;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.llm;
import roboslop.scene.transform;

namespace gorden {

// What the simulation reports back to the agent. Events are the only
// thing that makes the robot think: there is no think tick.
export enum class AgentEventKind : int {
    PlayerMessage,
    ToolRejected,
    MoveCompleted,
    InspectResult,
    Said,
    ProviderError,
};

export [[nodiscard]] constexpr auto eventKindName(AgentEventKind k) noexcept -> std::string_view {
    switch (k) {
    case AgentEventKind::PlayerMessage:
        return "player_message";
    case AgentEventKind::ToolRejected:
        return "tool_rejected";
    case AgentEventKind::MoveCompleted:
        return "move_completed";
    case AgentEventKind::InspectResult:
        return "inspect_result";
    case AgentEventKind::Said:
        return "said";
    case AgentEventKind::ProviderError:
        return "provider_error";
    }
    return "?";
}

export struct AgentEvent {
    AgentEventKind kind = AgentEventKind::PlayerMessage;
    std::string text{};
};

export struct TranscriptLine {
    std::string who; // "player" or "robot"
    std::string text{};
};

export struct BrainConfig {
    std::string model;           // empty → provider default
    std::size_t maxHistory = 24; // messages kept in working memory
    int maxChainedThinks = 4;    // per player message
    float observeRadius = 25.0F;
    Rules rules;
    std::string robotName = "Gorden";
    std::string playerName = "Player";
    // `{robot}` and `{player}` are replaced with the names above.
    std::string systemPrompt =
        "You are {robot}, the robot companion of {player} in a small locked room. You perceive "
        "the world only through the JSON observation in each user message. Act through "
        "the tools; use say to talk to {player} in one or two short sentences. "
        "Coordinates are metres, y is up. When a move completes or a tool is rejected you "
        "get a new observation; do not repeat a rejected action. "
        "You keep a memory between sessions: remember what you will want later, recall it "
        "by keywords before assuming you have forgotten, believe what you learn about "
        "things, and keep your intentions as goals — the active ones are listed in every "
        "observation and you close them yourself.";
};

export [[nodiscard]] auto renderSystemPrompt(const BrainConfig& cfg) -> std::string {
    std::string out = cfg.systemPrompt;
    for (const auto& [key, value] :
         {std::pair{std::string{"{robot}"}, cfg.robotName},
          std::pair{std::string{"{player}"}, cfg.playerName}}) {
        for (auto pos = out.find(key); pos != std::string::npos; pos = out.find(key, pos)) {
            out.replace(pos, key.size(), value);
            pos += value.size();
        }
    }
    return out;
}

// The agent loop for one robot:
//
//   events → observation → provider (async) → tool calls → validation
//          → commands applied to the world → events ...
//
// pump() runs on the main thread each fixed step. Inference never
// blocks it: a think is started with startCompletion and collected on a
// later pump. Everything that happened is written to `actionLog` — the
// validated sequence the M3 replay work will consume.
export class AgentBrain {
  public:
    AgentBrain(
        std::unique_ptr<roboslop::Provider> provider,
        BrainConfig config,
        roboslop::Entity robot,
        roboslop::Entity player
    )
        : provider(std::move(provider)), cfg(std::move(config)), robot(robot), player(player) {}

    AgentBrain(const AgentBrain&) = delete;
    auto operator=(const AgentBrain&) -> AgentBrain& = delete;
    AgentBrain(AgentBrain&&) noexcept = default;
    auto operator=(AgentBrain&&) noexcept -> AgentBrain& = default;
    ~AgentBrain() = default;

    auto playerSays(std::string text) -> void {
        transcriptLines.push_back({.who = "player", .text = text});
        chainedThinks = 0;
        capLogged = false;
        queueEvent({.kind = AgentEventKind::PlayerMessage, .text = std::move(text)});
    }

    // `dt` is the fixed step; the brain keeps its own simulation clock
    // so memories can be timestamped without a global time service.
    auto pump(roboslop::World& world, double dt = 0.0) -> void {
        simSeconds += dt;
        collectRobotEvents(world);
        if (pending.active()) {
            if (pending.ready()) {
                finishThink(world, pending.take());
            }
            return;
        }
        if (!events.empty()) {
            if (chainedThinks >= cfg.maxChainedThinks) {
                // Keep the events; they ride along with the next
                // player message instead of triggering yet another
                // round trip.
                if (!capLogged) {
                    log(std::format(
                        "think cap ({}) reached; waiting for the player", cfg.maxChainedThinks
                    ));
                    capLogged = true;
                }
                return;
            }
            startThink(world);
        }
    }

    // Live rename (pause menu's Settings page). Takes effect from the next think;
    // the transcript keeps role keys ("player"/"robot"), the UI maps
    // them to names.
    auto setNames(std::string robotName, std::string playerName) -> void {
        cfg.robotName = std::move(robotName);
        cfg.playerName = std::move(playerName);
    }

    [[nodiscard]] auto config() const noexcept -> const BrainConfig& {
        return cfg;
    }

    [[nodiscard]] auto robotEntity() const noexcept -> roboslop::Entity {
        return robot;
    }

    [[nodiscard]] auto playerEntity() const noexcept -> roboslop::Entity {
        return player;
    }

    [[nodiscard]] auto thinking() const noexcept -> bool {
        return pending.active();
    }

    [[nodiscard]] auto providerName() const noexcept -> std::string_view {
        return provider->name();
    }

    [[nodiscard]] auto transcript() const noexcept -> const std::vector<TranscriptLine>& {
        return transcriptLines;
    }

    [[nodiscard]] auto actionLog() const noexcept -> const std::vector<std::string>& {
        return actionLogLines;
    }

    [[nodiscard]] auto pendingEventCount() const noexcept -> std::size_t {
        return events.size();
    }

    [[nodiscard]] auto thinkCount() const noexcept -> unsigned {
        return thinks;
    }

    [[nodiscard]] auto memory() const noexcept -> const AgentMemory& {
        return mem;
    }

    // Loading a save replaces the long-term memory wholesale; the chat
    // history is not restored, so the robot resumes with what it chose
    // to remember rather than with the raw conversation.
    auto setMemory(AgentMemory memory, double simTime) -> void {
        mem = std::move(memory);
        simSeconds = simTime;
    }

    [[nodiscard]] auto simTime() const noexcept -> double {
        return simSeconds;
    }

  private:
    auto queueEvent(AgentEvent e) -> void {
        log(std::format("event {}: {}", eventKindName(e.kind), e.text));
        events.push_back(std::move(e));
    }

    // Perception plus the parts of memory the robot always carries:
    // active goals and beliefs are small and steer every decision, so
    // they ride along instead of waiting for a recall.
    [[nodiscard]] auto observe(const roboslop::World& world) const -> Observation {
        Observation obs = buildObservation(world, robot, player, cfg.observeRadius);
        for (const auto& g : mem.activeGoals()) {
            obs.goals.push_back({.id = g.id, .text = g.text});
        }
        for (const auto& b : mem.beliefs()) {
            obs.beliefs.push_back(
                std::format(
                    "{} {}: {} ({}, {:.2f})",
                    b.subject,
                    b.predicate,
                    b.value,
                    b.source.empty() ? "unknown source" : b.source,
                    b.confidence
                )
            );
        }
        return obs;
    }

    auto collectRobotEvents(roboslop::World& world) -> void {
        if (auto* m = world.tryGet<RobotMotion>(robot); m != nullptr && m->arrived) {
            m->arrived = false;
            const auto& t = world.get<roboslop::Transform>(robot);
            queueEvent({
                .kind = AgentEventKind::MoveCompleted,
                .text = std::format("arrived at ({:.1f}, {:.1f})", t.position.x, t.position.z),
            });
        }
    }

    auto startThink(roboslop::World& world) -> void {
        Observation obs = observe(world);
        if (const auto* m = world.tryGet<RobotMotion>(robot); m != nullptr) {
            obs.robotMoving = m->target.has_value();
        }
        for (auto& e : events) {
            if (e.kind == AgentEventKind::PlayerMessage) {
                obs.playerMessage = e.text;
            } else {
                obs.recentEvents.push_back(std::format("{}: {}", eventKindName(e.kind), e.text));
            }
        }
        events.clear();

        history.push_back({.role = roboslop::Role::User, .content = observationToJson(obs)});
        trimHistory();

        roboslop::ChatRequest req{.model = cfg.model, .tools = toolSpecs()};
        req.messages.reserve(history.size() + 1);
        req.messages.push_back(
            {.role = roboslop::Role::System, .content = renderSystemPrompt(cfg)}
        );
        req.messages.insert(req.messages.end(), history.begin(), history.end());

        ++thinks;
        ++chainedThinks;
        log(std::format(
            "think #{} started ({} nearby, {} events)",
            thinks,
            obs.nearby.size(),
            obs.recentEvents.size()
        ));
        pending = roboslop::startCompletion(*provider, std::move(req));
    }

    auto finishThink(roboslop::World& world, roboslop::Result<roboslop::ChatResponse> result)
        -> void {
        if (!result) {
            const auto& e = result.error();
            const std::string text = std::format("{} ({})", e.message, e.context);
            log(std::format("provider error: {}", text));
            spdlog::warn("gorden: provider error: {}", text);
            // Not queued as an event on purpose: an error must not
            // trigger another think and loop against a dead server.
            transcriptLines.push_back({.who = "robot", .text = "[provider error: " + text + "]"});
            // Drop the observation we sent so the next think re-sends
            // a fresh one instead of two users in a row.
            if (!history.empty() && history.back().role == roboslop::Role::User) {
                history.pop_back();
            }
            return;
        }

        const roboslop::ChatResponse& resp = *result;
        history.push_back({
            .role = roboslop::Role::Assistant,
            .content = resp.content,
            .toolCalls = resp.toolCalls,
        });
        if (!resp.content.empty()) {
            // Plain assistant text is treated as speech too, so a model
            // that answers without calling `say` is still heard.
            transcriptLines.push_back({.who = "robot", .text = resp.content});
            log(std::format("assistant text: {}", resp.content));
        }

        const Observation obs = observe(world);
        for (const auto& call : resp.toolCalls) {
            auto parsed = parseToolCall(call);
            auto validated = parsed ? validate(*parsed, obs, cfg.rules)
                                    : roboslop::Result<Command>{std::unexpected(parsed.error())};
            std::string toolResult{};
            if (!validated) {
                const auto& e = validated.error();
                toolResult = std::format("rejected: {} ({})", e.message, e.context);
                log(std::format(
                    "proposed {} {} → REJECTED: {} {}",
                    call.name,
                    call.argumentsJson,
                    e.message,
                    e.context
                ));
                queueEvent(
                    {.kind = AgentEventKind::ToolRejected,
                     .text = std::format("{}: {}", call.name, e.message)}
                );
            } else {
                log(std::format("proposed {} → accepted", describeCommand(*validated)));
                toolResult = apply(world, *validated, obs);
            }
            history.push_back({
                .role = roboslop::Role::Tool,
                .content = toolResult,
                .toolCallId = call.id,
            });
        }
        trimHistory();
    }

    // Executes a validated command and returns the tool-result text
    // the model sees. Side effects on the world happen here and only
    // here.
    auto apply(roboslop::World& world, const Command& cmd, const Observation& obs) -> std::string {
        return std::visit(
            [&](const auto& c) -> std::string {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, MoveTo>) {
                    auto& m = world.get<RobotMotion>(robot);
                    m.target = c.target;
                    m.arrived = false;
                    // No event now: MoveCompleted arrives from the
                    // locomotion system when the robot gets there.
                    return "accepted: moving; you will be told when you arrive";
                } else if constexpr (std::is_same_v<T, Inspect>) {
                    const std::string text = inspect(world, c, obs);
                    queueEvent({.kind = AgentEventKind::InspectResult, .text = text});
                    return text;
                } else if constexpr (std::is_same_v<T, Say>) {
                    transcriptLines.push_back({.who = "robot", .text = c.text});
                    log(std::format("said: {}", c.text));
                    // Speech needs no follow-up think; it is not queued
                    // as an event.
                    return "said";
                } else if constexpr (std::is_same_v<T, Remember>) {
                    // Memory writes need no follow-up think either: the
                    // robot already knows what it just stored.
                    const auto& episode = mem.remember(c.text, thinks, simSeconds);
                    log(std::format("remembered {}: {}", episode.id, episode.text));
                    return std::format("remembered as {}", episode.id);
                } else if constexpr (std::is_same_v<T, Recall>) {
                    const auto hits = mem.recall(c.query, c.limit);
                    log(std::format("recall \"{}\" → {} hit(s)", c.query, hits.size()));
                    if (hits.empty()) {
                        return "no memory matches that";
                    }
                    std::string text;
                    for (const auto& e : hits) {
                        text += std::format("{} ({:.0f}s): {}\n", e.id, e.at, e.text);
                    }
                    text.pop_back();
                    return text;
                } else if constexpr (std::is_same_v<T, Believe>) {
                    mem.believe({
                        .subject = c.subject,
                        .predicate = c.predicate,
                        .value = c.value,
                        .source = c.source,
                        .learnedAt = simSeconds,
                        .confidence = c.confidence,
                    });
                    log(std::format(
                        "believe {} {} = {} ({:.2f})", c.subject, c.predicate, c.value, c.confidence
                    ));
                    return "noted";
                } else if constexpr (std::is_same_v<T, SetGoal>) {
                    const auto& goal = mem.setGoal(c.text, simSeconds);
                    log(std::format("goal {} set: {}", goal.id, goal.text));
                    return std::format("goal {} is active", goal.id);
                } else {
                    // validate() already checked the id against the same
                    // goal list, so a false here would be a bug, not a
                    // model mistake.
                    const bool closed = mem.closeGoal(c.id, c.status);
                    log(std::format(
                        "goal {} {}", c.id, closed ? goalStatusName(c.status) : "not found"
                    ));
                    return closed ? std::format("goal {} closed", c.id) : "no such goal";
                }
            },
            cmd
        );
    }

    // Position and state from anywhere in range; the detail only up
    // close, and reading it is recorded on the entity for gameplay.
    auto inspect(roboslop::World& world, const Inspect& c, const Observation& obs) const
        -> std::string {
        const auto seen = std::ranges::find(obs.nearby, c.name, &ObservedEntity::name);
        if (seen == obs.nearby.end()) {
            return "not visible";
        }
        std::string text = std::format(
            "{} is at ({:.1f}, {:.1f}), {:.1f} m away",
            seen->name,
            seen->position.x,
            seen->position.z,
            seen->distance
        );
        if (!seen->state.empty()) {
            text += std::format("; state: {}", seen->state);
        }
        auto* inspectable = world.tryGet<Inspectable>(seen->entity);
        if (inspectable == nullptr || inspectable->detail.empty()) {
            return text;
        }
        const glm::vec2 offset{
            seen->position.x - obs.robotPosition.x, seen->position.z - obs.robotPosition.z
        };
        if (glm::length(offset) > cfg.rules.inspectReach) {
            return text +
                   std::format(
                       ". Too far to make out details; move within {:.1f} m", cfg.rules.inspectReach
                   );
        }
        inspectable->inspected = true;
        return text + ". " + inspectable->detail;
    }

    // Working memory is bounded. Drop oldest messages, then make sure
    // the window does not start with orphaned tool results (the API
    // rejects a Tool message whose assistant call was trimmed away).
    auto trimHistory() -> void {
        while (history.size() > cfg.maxHistory) {
            history.pop_front();
        }
        while (!history.empty() && history.front().role == roboslop::Role::Tool) {
            history.pop_front();
        }
    }

    auto log(std::string line) -> void {
        spdlog::info("gorden agent: {}", line);
        actionLogLines.push_back(std::move(line));
    }

    std::unique_ptr<roboslop::Provider> provider;
    BrainConfig cfg;
    roboslop::Entity robot{};
    roboslop::Entity player{};
    std::deque<roboslop::ChatMessage> history;
    std::vector<AgentEvent> events{};
    roboslop::AsyncCompletion pending;
    std::vector<TranscriptLine> transcriptLines{};
    std::vector<std::string> actionLogLines{};
    AgentMemory mem{};
    double simSeconds = 0.0;
    unsigned thinks = 0;
    int chainedThinks = 0;
    bool capLogged = false;
};

} // namespace gorden
