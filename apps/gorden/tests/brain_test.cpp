import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.scene.transform;

#include <catch2/catch_test_macros.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Fixture {
    roboslop::World world;
    roboslop::Entity robot{};
    roboslop::Entity player{};

    Fixture() : robot(world.create()), player(world.create()) {

        world.emplace<roboslop::Transform>(
            robot, roboslop::Transform{.position = {0.0F, 0.5F, 0.0F}}
        );
        world.emplace<gorden::Named>(robot, gorden::Named{.name = "robot"});
        world.emplace<gorden::Robot>(robot);
        world.emplace<gorden::RobotMotion>(robot, gorden::RobotMotion{.speed = 10.0F});

        world.emplace<roboslop::Transform>(
            player, roboslop::Transform{.position = {3.0F, 1.0F, 0.0F}}
        );
        world.emplace<gorden::Named>(player, gorden::Named{.name = "player"});

        const auto crate = world.create();
        world.emplace<roboslop::Transform>(
            crate, roboslop::Transform{.position = {2.0F, 0.0F, 2.0F}}
        );
        world.emplace<gorden::Named>(crate, gorden::Named{.name = "crate-1"});
    }
};

// Pumps until the in-flight think has been collected (or gives up).
auto pumpUntilIdle(gorden::AgentBrain& brain, roboslop::World& world) -> void {
    for (int i = 0; i < 1000; ++i) {
        brain.pump(world);
        if (!brain.thinking()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}

auto logContains(const gorden::AgentBrain& brain, const std::string& needle) -> bool {
    return std::ranges::any_of(brain.actionLog(), [&](const std::string& line) {
        return line.contains(needle);
    });
}

auto toolCallResponse(std::string name, std::string args, std::string id = "c1")
    -> roboslop::ChatResponse {
    return roboslop::ChatResponse{
        .toolCalls =
            {{.id = std::move(id), .name = std::move(name), .argumentsJson = std::move(args)}},
        .finishReason = "tool_calls",
    };
}

} // namespace

TEST_CASE("player message triggers a think; accepted moveTo drives the robot", "[agent][brain]") {
    Fixture f;
    auto provider = std::make_unique<roboslop::ScriptedProvider>(
        std::vector<roboslop::ChatResponse>{toolCallResponse("moveTo", R"({"x": 2, "z": 0})")}
    );
    auto* scripted = provider.get();
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);

    brain.pump(f.world);
    REQUIRE(brain.thinkCount() == 0); // nothing happened yet

    brain.playerSays("go to the crate");
    pumpUntilIdle(brain, f.world);

    REQUIRE(brain.thinkCount() == 1);
    const auto& m = f.world.get<gorden::RobotMotion>(f.robot);
    REQUIRE(m.target.has_value());
    REQUIRE(m.target->x == 2.0F);
    REQUIRE(logContains(brain, "moveTo(2.0, 0.0) → accepted"));

    // The observation the model received carried the player's words.
    const auto requests = scripted->recordedRequests();
    REQUIRE(requests.size() == 1);
    REQUIRE(requests[0].messages.front().role == roboslop::Role::System);
    REQUIRE(requests[0].messages.back().content.contains("go to the crate"));
    REQUIRE(requests[0].tools.size() == 8);
}

TEST_CASE(
    "arrival raises MoveCompleted and a follow-up think with the tool result", "[agent][brain]"
) {
    Fixture f;
    auto provider =
        std::make_unique<roboslop::ScriptedProvider>(std::vector<roboslop::ChatResponse>{
            toolCallResponse("moveTo", R"({"x": 1, "z": 0})"),
            toolCallResponse("say", R"({"text": "I am here."})", "c2"),
        });
    auto* scripted = provider.get();
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);

    brain.playerSays("come here");
    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.thinkCount() == 1);

    // Simulate the locomotion system reaching the target.
    auto& m = f.world.get<gorden::RobotMotion>(f.robot);
    auto& t = f.world.get<roboslop::Transform>(f.robot);
    while (!gorden::tickRobotMotion(t, m, 0.1F)) {
    }
    m.arrived = true;

    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.thinkCount() == 2);
    REQUIRE(logContains(brain, "event move_completed"));

    const auto requests = scripted->recordedRequests();
    REQUIRE(requests.size() == 2);
    // Second request replays: user obs, assistant tool call, tool result, new user obs.
    const auto& msgs = requests[1].messages;
    REQUIRE(msgs.size() == 5);
    REQUIRE(msgs[2].role == roboslop::Role::Assistant);
    REQUIRE(msgs[2].toolCalls.size() == 1);
    REQUIRE(msgs[3].role == roboslop::Role::Tool);
    REQUIRE(msgs[3].toolCallId == "c1");
    REQUIRE(msgs[4].content.contains("move_completed"));

    // The second think said something; speech does not trigger a third.
    REQUIRE(brain.transcript().back().who == "robot");
    REQUIRE(brain.transcript().back().text == "I am here.");
    brain.pump(f.world);
    REQUIRE_FALSE(brain.thinking());
    REQUIRE(brain.thinkCount() == 2);
}

TEST_CASE("rejected tool calls are logged and leave the world untouched", "[agent][brain]") {
    Fixture f;
    auto provider = std::make_unique<roboslop::ScriptedProvider>(
        std::vector<roboslop::ChatResponse>{toolCallResponse("moveTo", R"({"x": 500, "z": 0})")}
    );
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);

    brain.playerSays("go far away");
    pumpUntilIdle(brain, f.world);
    REQUIRE_FALSE(f.world.get<gorden::RobotMotion>(f.robot).target.has_value());
    REQUIRE(logContains(brain, "REJECTED"));
    // The rejection is an event, so a follow-up think is started.
    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.thinkCount() == 2);
}

TEST_CASE("chained thinks are capped per player message", "[agent][brain]") {
    Fixture f;
    // Every answer inspects the crate, which yields an InspectResult
    // event and would loop forever without the cap.
    std::vector<roboslop::ChatResponse> script;
    script.reserve(10);
    for (int i = 0; i < 10; ++i) {
        script.push_back(
            toolCallResponse("inspect", R"({"name": "crate-1"})", "c" + std::to_string(i))
        );
    }
    auto provider = std::make_unique<roboslop::ScriptedProvider>(std::move(script));
    gorden::BrainConfig cfg;
    cfg.maxChainedThinks = 3;
    gorden::AgentBrain brain(std::move(provider), cfg, f.robot, f.player);

    brain.playerSays("look around");
    for (int i = 0; i < 20; ++i) {
        pumpUntilIdle(brain, f.world);
    }
    REQUIRE(brain.thinkCount() == 3);
    REQUIRE(brain.pendingEventCount() == 1);
    REQUIRE(logContains(brain, "think cap"));

    // A new player message resets the budget.
    brain.playerSays("again");
    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.thinkCount() == 4);
}

TEST_CASE("provider errors are reported and do not loop", "[agent][brain]") {
    struct FailingProvider final : roboslop::Provider {
        auto complete(const roboslop::ChatRequest&)
            -> roboslop::Result<roboslop::ChatResponse> override {
            return std::unexpected(roboslop::toError(roboslop::LlmError::TransportFailed, "down"));
        }

        [[nodiscard]] auto name() const noexcept -> std::string_view override {
            return "failing";
        }
    };

    Fixture f;
    gorden::AgentBrain brain(
        std::make_unique<FailingProvider>(), gorden::BrainConfig{}, f.robot, f.player
    );
    brain.playerSays("hello?");
    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.thinkCount() == 1);
    REQUIRE(logContains(brain, "provider error"));
    brain.pump(f.world);
    REQUIRE_FALSE(brain.thinking());
    REQUIRE(brain.thinkCount() == 1);
    REQUIRE(brain.transcript().back().text.contains("provider error"));
}

TEST_CASE("remembered things come back through recall in a later think", "[agent][brain]") {
    Fixture f;
    auto provider =
        std::make_unique<roboslop::ScriptedProvider>(std::vector<roboslop::ChatResponse>{
            toolCallResponse("remember", R"({"text": "the crate hides a key"})"),
            toolCallResponse("recall", R"({"query": "crate"})", "c2"),
        });
    auto* scripted = provider.get();
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);

    brain.playerSays("the crate hides a key");
    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.memory().episodes().size() == 1);
    REQUIRE(logContains(brain, "remembered ep-1"));

    brain.playerSays("what was in the crate?");
    pumpUntilIdle(brain, f.world);

    // The recall result reached the model as the tool message, and the
    // episode itself never had to sit in the chat history.
    const auto requests = scripted->recordedRequests();
    REQUIRE(requests.size() == 2);
    const auto& replayed = requests[1].messages;
    const bool toolResultCarriesTheMemory =
        std::ranges::any_of(replayed, [](const roboslop::ChatMessage& m) {
            return m.role == roboslop::Role::Tool && m.content.contains("the crate hides a key");
        });
    REQUIRE(brain.memory().recall("crate", 5).size() == 1);
    REQUIRE(logContains(brain, "recall \"crate\" → 1 hit(s)"));
    REQUIRE_FALSE(toolResultCarriesTheMemory); // it lands after this request
}

TEST_CASE("an active goal rides along in the next observation", "[agent][brain]") {
    Fixture f;
    auto provider =
        std::make_unique<roboslop::ScriptedProvider>(std::vector<roboslop::ChatResponse>{
            toolCallResponse("setGoal", R"({"text": "find the key"})"),
            toolCallResponse("closeGoal", R"({"id": "goal-1", "status": "done"})", "c2"),
        });
    auto* scripted = provider.get();
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);

    brain.playerSays("find the key");
    pumpUntilIdle(brain, f.world);
    REQUIRE(brain.memory().activeGoals().size() == 1);

    brain.playerSays("did you?");
    pumpUntilIdle(brain, f.world);

    const auto requests = scripted->recordedRequests();
    REQUIRE(requests.size() == 2);
    REQUIRE(requests[1].messages.back().content.contains("goal-1"));
    REQUIRE(brain.memory().activeGoals().empty());
    REQUIRE(brain.memory().goals()[0].status == gorden::GoalStatus::Done);
}

TEST_CASE("a belief is rendered into every later observation", "[agent][brain]") {
    Fixture f;
    auto provider =
        std::make_unique<roboslop::ScriptedProvider>(std::vector<roboslop::ChatResponse>{
            toolCallResponse(
                "believe",
                R"({"subject":"crate","predicate":"contents","value":"a key","source":"player"})"
            ),
            toolCallResponse("say", R"({"text": "Noted."})", "c2"),
        });
    auto* scripted = provider.get();
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);

    brain.playerSays("the crate holds a key");
    pumpUntilIdle(brain, f.world);
    brain.playerSays("and?");
    pumpUntilIdle(brain, f.world);

    const auto requests = scripted->recordedRequests();
    REQUIRE(requests[1].messages.back().content.contains("crate contents: a key (player"));
    REQUIRE(brain.memory().beliefs().size() == 1);
}

TEST_CASE("inspect reads details only within reach", "[agent][brain]") {
    Fixture f;
    const auto bay = f.world.create();
    f.world.emplace<roboslop::Transform>(bay, roboslop::Transform{.position = {-3.0F, 0.4F, 0.0F}});
    f.world.emplace<gorden::Named>(bay, gorden::Named{.name = "Conduit bay C"});
    f.world.emplace<gorden::Inspectable>(
        bay, gorden::Inspectable{.state = "sealed", .detail = "Maintenance tag C-17."}
    );
    auto provider =
        std::make_unique<roboslop::ScriptedProvider>(std::vector<roboslop::ChatResponse>{
            toolCallResponse("inspect", R"({"name": "Conduit bay C"})", "c1"),
            toolCallResponse("inspect", R"({"name": "Conduit bay C"})", "c2"),
        });
    auto* scripted = provider.get();
    gorden::AgentBrain brain(std::move(provider), gorden::BrainConfig{}, f.robot, f.player);
    brain.playerSays("what does the bay say?");
    pumpUntilIdle(brain, f.world);
    REQUIRE_FALSE(f.world.get<gorden::Inspectable>(bay).inspected);

    // Within reach horizontally, even though the panel sits above the floor.
    f.world.get<roboslop::Transform>(f.robot).position = {-2.2F, 0.0F, 0.3F};
    pumpUntilIdle(brain, f.world); // the InspectResult event starts the second think
    REQUIRE(f.world.get<gorden::Inspectable>(bay).inspected);

    // A third think shows the model both tool results.
    brain.playerSays("thanks");
    pumpUntilIdle(brain, f.world);
    const auto requests = scripted->recordedRequests();
    REQUIRE(requests.size() == 3);
    std::vector<std::string> results;
    for (const auto& m : requests[2].messages) {
        if (m.role == roboslop::Role::Tool) {
            results.push_back(m.content);
        }
    }
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].contains("state: sealed"));
    REQUIRE(results[0].contains("Too far"));
    REQUIRE_FALSE(results[0].contains("C-17"));
    REQUIRE(results[1].contains("C-17"));
}
