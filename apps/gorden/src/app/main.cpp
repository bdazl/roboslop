import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import gorden.save;
import gorden.player;
import gorden.player_visual;
import gorden.robot_visual;
import gorden.settings;
import gorden.llm_config;
import roboslop.app;
import roboslop.audio;
import roboslop.core.error;
import roboslop.core.paths;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.platform.input;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.context;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.lighting;
import roboslop.render.model;
import roboslop.scene.transform;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.savegame;
import roboslop.sched;
import roboslop.shell;
import roboslop.ui;
import roboslop.ui.terminal;
import roboslop.vfs;

#include <bgfx/bgfx.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// Per-frame UI state for the Robot panel, parked in the world context.
struct RobotPanelState {
    std::array<char, 256> input{};
    bool scrollTranscript = false;
    std::size_t seenLines = 0;
};

// Settings as loaded/edited, plus the edit buffers for the Settings
// window and the last window-visibility snapshot (saved on change).
struct SettingsState {
    gorden::GordenSettings settings;
    std::filesystem::path path;
    std::array<char, 64> playerBuf{};
    std::array<char, 64> robotBuf{};
    std::map<std::string, bool> lastVisibility;
    std::string status;
};

// Saving and loading are explicit, and both need the AssetCache, which
// only a render pass gets. The UI and the shell therefore record a
// request here and the "robotChat" pass carries it out.
struct SaveState {
    enum class Request : std::uint8_t {
        None,
        Save,
        Load,
    };

    std::string scenePath{};            // as loaded at startup
    roboslop::SceneDocument document{}; // the authored scene to rebuild from
    std::string slot = "default";
    std::array<char, 64> slotBuf{};
    Request request = Request::None;
    std::string status{};
};

auto copyToBuffer(std::array<char, 64>& buf, const std::string& text) -> void {
    buf.fill('\0');
    std::strncpy(buf.data(), text.c_str(), buf.size() - 1);
}

// Pushes the names in `st.settings` into the world: Named components
// and the brain's prompt.
auto applyNames(roboslop::World& world, SettingsState& st, gorden::AgentBrain& brain) -> void {
    world.get<gorden::Named>(brain.robotEntity()).name = st.settings.robotName;
    world.get<gorden::Named>(brain.playerEntity()).name = st.settings.playerName;
    brain.setNames(st.settings.robotName, st.settings.playerName);
}

auto saveSettingsNow(SettingsState& st) -> void {
    if (auto r = gorden::saveSettings(st.path, st.settings); !r) {
        st.status = std::format("save failed: {} ({})", r.error().message, r.error().context);
        spdlog::warn("gorden: {}", st.status);
    } else {
        st.status = "saved to " + st.path.string();
    }
}

// Runs where the AssetCache is available. Returns a one-line result
// for the UI, the terminal and the log to share.
auto performSaveRequest(
    roboslop::World& world, roboslop::AssetCache& assets, SaveState& st, SaveState::Request request
) -> std::string {
    auto& brain = world.registry().ctx().get<gorden::AgentBrain>();
    const auto path = gorden::savePath(st.slot);
    if (request == SaveState::Request::Save) {
        const auto save = gorden::captureSave(world, brain, st.scenePath);
        if (auto written = roboslop::saveSaveGame(path, save); !written) {
            return std::format(
                "save failed: {} ({})", written.error().message, written.error().context
            );
        }
        return "saved to " + path.string();
    }
    auto loaded = roboslop::loadSaveGame(path);
    if (!loaded) {
        return std::format("load failed: {} ({})", loaded.error().message, loaded.error().context);
    }
    auto& runtime = world.registry().ctx().get<roboslop::SceneRuntime>();
    if (auto applied = gorden::applySave(world, assets, runtime, st.document, brain, *loaded);
        !applied) {
        return std::format(
            "load failed: {} ({})", applied.error().message, applied.error().context
        );
    }
    return "loaded " + path.string();
}

auto readNameBuffers(SettingsState& st) -> void {
    if (st.playerBuf[0] != '\0') {
        st.settings.playerName = st.playerBuf.data();
    }
    if (st.robotBuf[0] != '\0') {
        st.settings.robotName = st.robotBuf.data();
    }
}

auto drawSettingsPanel(roboslop::World& world, SettingsState& st, gorden::AgentBrain& brain)
    -> void {
    ImGui::InputText("Player name", st.playerBuf.data(), st.playerBuf.size());
    ImGui::InputText("Robot name", st.robotBuf.data(), st.robotBuf.size());
    if (ImGui::Button("Apply")) {
        readNameBuffers(st);
        applyNames(world, st, brain);
        st.status = "applied";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        readNameBuffers(st);
        applyNames(world, st, brain);
        saveSettingsNow(st);
    }
    ImGui::TextDisabled("%s", st.path.string().c_str());
    if (!st.status.empty()) {
        ImGui::TextUnformatted(st.status.c_str());
    }

    ImGui::SeparatorText("Save game");
    auto& save = world.registry().ctx().get<SaveState>();
    if (ImGui::InputText("Slot", save.slotBuf.data(), save.slotBuf.size()) &&
        save.slotBuf[0] != '\0') {
        save.slot = save.slotBuf.data();
    }
    if (ImGui::Button("Save game")) {
        save.request = SaveState::Request::Save;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load game")) {
        save.request = SaveState::Request::Load;
    }
    ImGui::TextDisabled("%s", gorden::savePath(save.slot).string().c_str());
    if (!save.status.empty()) {
        ImGui::TextUnformatted(save.status.c_str());
    }
}

// The key file should be private to the user (chmod 600). Only a
// warning: the app never changes permissions on files it does not own.
auto warnIfKeyFileIsShared(const std::filesystem::path& path) -> void {
    std::error_code ec;
    const auto perms = std::filesystem::status(path, ec).permissions();
    constexpr auto Shared =
        std::filesystem::perms::group_read | std::filesystem::perms::others_read;
    if (!ec && (perms & Shared) != std::filesystem::perms::none) {
        spdlog::warn("gorden: {} is readable by other users; consider chmod 600", path.string());
    }
}

// A key in configDir()/llm.json or OPENAI_API_KEY → the OpenAI-compatible
// backend (model and base URL from the same two places, so the same
// code targets a llama.cpp server). Otherwise a scripted demo so the
// whole chain still runs. The key is read once and never logged.
auto makeProvider() -> std::unique_ptr<roboslop::Provider> {
    const auto path = gorden::llmConfigPath();
    gorden::LlmConfig llm;
    if (auto loaded = gorden::loadLlmConfig(path); loaded) {
        llm = std::move(*loaded);
    } else {
        spdlog::warn(
            "gorden: {} unreadable ({}); ignoring it", path.string(), loaded.error().context
        );
    }
    if (!llm.apiKey.empty()) {
        warnIfKeyFileIsShared(path);
    }
    gorden::applyEnvOverrides(llm);
    if (!llm.apiKey.empty()) {
        roboslop::OpenAiConfig cfg;
        cfg.apiKey = std::move(llm.apiKey);
        if (!llm.model.empty()) {
            cfg.model = llm.model;
        }
        if (!llm.baseUrl.empty()) {
            cfg.baseUrl = llm.baseUrl;
        }
        spdlog::info(
            "gorden: LLM backend openai-compatible, model={}, base={}", cfg.model, cfg.baseUrl
        );
        return std::make_unique<roboslop::OpenAiProvider>(std::move(cfg));
    }
    spdlog::warn(
        "gorden: no API key in {} or OPENAI_API_KEY; using the scripted demo provider",
        path.string()
    );
    auto say = [](std::string text, std::string id) {
        return roboslop::ToolCall{
            .id = std::move(id),
            .name = "say",
            .argumentsJson = R"({"text":")" + std::move(text) + "\"}",
        };
    };
    std::vector<roboslop::ChatResponse> script{
        roboslop::ChatResponse{
            .toolCalls =
                {say("Hello! I am a scripted robot. Watch me walk to the generator.", "s1"),
                 {.id = "s2", .name = "moveTo", .argumentsJson = R"({"x":0,"z":-4.5})"}},
            .finishReason = "tool_calls",
        },
        roboslop::ChatResponse{
            .toolCalls =
                {say("I am at the generator. Put a key in llm.json for a real brain.", "s3")},
            .finishReason = "tool_calls",
        },
    };
    return std::make_unique<roboslop::ScriptedProvider>(
        std::move(script),
        roboslop::ChatResponse{
            .toolCalls = {say("(scripted) I have run out of script.", "s0")},
            .finishReason = "tool_calls",
        }
    );
}

auto drawRobotPanel(roboslop::World& world, gorden::AgentBrain& brain, RobotPanelState& st)
    -> void {

    ImGui::TextUnformatted(
        std::format(
            "provider: {}   state: {}   thinks: {}",
            brain.providerName(),
            brain.thinking() ? "Thinking" : "Idle",
            brain.thinkCount()
        )
            .c_str()
    );
    ImGui::Separator();

    // The transcript fills the window above one row of input, so the
    // Send row follows the window's size. Scrolling follows new lines
    // (ours or the robot's) unless the user has scrolled up to read.
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("transcript", ImVec2(0.0F, -footer), ImGuiChildFlags_Border);
    const auto& names = brain.config();
    const bool nearBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F;
    const std::size_t lineCount = brain.transcript().size();
    for (const auto& line : brain.transcript()) {
        const bool robot = line.who == "robot";
        ImGui::PushStyleColor(
            ImGuiCol_Text, robot ? ImVec4(0.6F, 0.9F, 1.0F, 1.0F) : ImVec4(0.9F, 0.9F, 0.9F, 1.0F)
        );
        ImGui::TextWrapped(
            "%s: %s", (robot ? names.robotName : names.playerName).c_str(), line.text.c_str()
        );
        ImGui::PopStyleColor();
    }
    if (st.scrollTranscript || (lineCount != st.seenLines && nearBottom)) {
        ImGui::SetScrollHereY(1.0F);
        st.scrollTranscript = false;
    }
    st.seenLines = lineCount;
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-80.0F);
    const bool entered = ImGui::InputText(
        "##say", st.input.data(), st.input.size(), ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::SameLine();
    const bool clicked = ImGui::Button("Send", ImVec2(-1.0F, 0.0F));
    if ((entered || clicked) && st.input[0] != '\0') {
        brain.playerSays(std::string{st.input.data()});
        st.input.fill('\0');
        st.scrollTranscript = true;
        ImGui::SetKeyboardFocusHere(-1);
    }

    (void)world;
}

// The validated action log: every observation delivered, proposal,
// verdict, and resulting event. Its own window so the chat stays
// readable; the same lines back /var/log/agent.log in the terminal.
struct AgentLogState {
    std::array<char, 64> filter{};
    bool autoScroll = true;
    std::size_t hiddenBefore = 0; // "Clear" hides older lines without touching the brain
};

auto drawAgentLogPanel(gorden::AgentBrain& brain, AgentLogState& st) -> void {
    ImGui::SetNextItemWidth(220.0F);
    ImGui::InputTextWithHint("##filter", "filter", st.filter.data(), st.filter.size());
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &st.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        st.hiddenBefore = brain.actionLog().size();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu lines", brain.actionLog().size());
    ImGui::Separator();

    ImGui::BeginChild("lines", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Border);
    const std::string_view filter{st.filter.data()};
    const auto& log = brain.actionLog();
    for (std::size_t i = st.hiddenBefore; i < log.size(); ++i) {
        if (!filter.empty() && !log[i].contains(filter)) {
            continue;
        }
        ImGui::TextWrapped("%s", log[i].c_str());
    }
    if (st.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F) {
        ImGui::SetScrollHereY(1.0F);
    }
    ImGui::EndChild();
}

// The debug terminal's view of the app: live files over the brain and
// settings, a writable in-memory home, and one host-mounted directory
// that persists. Everything is read on demand; nothing is copied.
auto mountGordenFiles(roboslop::World& world, roboslop::Vfs& fs) -> void {
    auto* w = &world;
    const auto& st = world.registry().ctx().get<SettingsState>();
    const std::string home = "/home/" + st.settings.playerName;
    (void)fs.mkdir(home, true);
    (void)fs.mkdir("/tmp", true);
    (void)fs.writeFile(
        home + "/README",
        "This is Gorden's inside. Try:\n"
        "  tail -f /var/log/agent.log\n"
        "  cat /proc/gorden/observation\n"
        "  cat /etc/gorden/settings.json\n"
        "  echo note > /persist/note.txt   (survives restarts)\n"
    );

    (void)fs.mountLive(
        "/var/log/agent.log",
        roboslop::LiveFile{
            .read =
                [w] {
                    std::string out;
                    for (const auto& line :
                         w->registry().ctx().get<gorden::AgentBrain>().actionLog()) {
                        out += line;
                        out += '\n';
                    }
                    return out;
                },
            .write = {},
        }
    );
    (void)fs.mountLive(
        "/proc/gorden/observation",
        roboslop::LiveFile{
            .read =
                [w] {
                    const auto& brain = w->registry().ctx().get<gorden::AgentBrain>();
                    return gorden::observationToJson(
                               gorden::buildObservation(
                                   *w,
                                   brain.robotEntity(),
                                   brain.playerEntity(),
                                   brain.config().observeRadius
                               )
                           ) +
                           "\n";
                },
            .write = {},
        }
    );
    (void)fs.mountLive(
        "/proc/gorden/transcript",
        roboslop::LiveFile{
            .read =
                [w] {
                    const auto& brain = w->registry().ctx().get<gorden::AgentBrain>();
                    std::string out;
                    for (const auto& line : brain.transcript()) {
                        const bool robot = line.who == "robot";
                        out += (robot ? brain.config().robotName : brain.config().playerName) +
                               ": " + line.text + "\n";
                    }
                    return out;
                },
            .write = {},
        }
    );
    (void)fs.mountLive(
        "/proc/gorden/status",
        roboslop::LiveFile{
            .read =
                [w] {
                    const auto& brain = w->registry().ctx().get<gorden::AgentBrain>();
                    return std::format(
                        "provider: {}\nstate: {}\nthinks: {}\npending events: {}\n",
                        brain.providerName(),
                        brain.thinking() ? "thinking" : "idle",
                        brain.thinkCount(),
                        brain.pendingEventCount()
                    );
                },
            .write = {},
        }
    );
    (void)fs.mountLive(
        "/etc/gorden/settings.json",
        roboslop::LiveFile{
            .read =
                [w] {
                    return gorden::toJson(w->registry().ctx().get<SettingsState>().settings)
                               .dump(2) +
                           "\n";
                },
            .write = [w](std::string_view text) -> roboslop::Result<void> {
                auto& c = w->registry().ctx();
                auto& state = c.get<SettingsState>();
                auto parsed = gorden::settingsFromJsonText(text, state.settings);
                if (!parsed) {
                    return std::unexpected(parsed.error());
                }
                state.settings = std::move(*parsed);
                copyToBuffer(state.playerBuf, state.settings.playerName);
                copyToBuffer(state.robotBuf, state.settings.robotName);
                applyNames(*w, state, c.get<gorden::AgentBrain>());
                saveSettingsNow(state);
                return {};
            },
        }
    );
    (void)fs.mountLive(
        "/proc/gorden/memory",
        roboslop::LiveFile{
            .read =
                [w] {
                    const auto& brain = w->registry().ctx().get<gorden::AgentBrain>();
                    return gorden::toJson(brain.memory()).dump(2) + "\n";
                },
            .write = {},
        }
    );
    (void)fs.mountHost("/persist", roboslop::dataDir() / "gorden");
}

} // namespace

auto main(int argc, char** argv) -> int {
    std::filesystem::path scenePath = "assets/scenes/room.json";
    if (argc == 3 && std::string_view{argv[1]} == "--scene") {
        scenePath = argv[2];
    } else if (argc != 1) {
        std::println(stderr, "Usage: gorden [--scene path]");
        return 1;
    }
    auto scene = roboslop::loadScene(scenePath);
    if (!scene) {
        std::println(
            stderr, "Cannot load scene: {} ({})", scene.error().message, scene.error().context
        );
        return 1;
    }
    // Settings first: names are needed while the scene is built.
    SettingsState initial;
    initial.path = gorden::settingsPath();
    if (auto loaded = gorden::loadSettings(initial.path); loaded) {
        initial.settings = std::move(*loaded);
    } else {
        spdlog::warn("gorden: settings unreadable ({}); using defaults", loaded.error().context);
    }
    copyToBuffer(initial.playerBuf, initial.settings.playerName);
    copyToBuffer(initial.robotBuf, initial.settings.robotName);

    auto app = roboslop::App::make(
        roboslop::AppConfig{
            .window = roboslop::WindowConfig{.title = "gorden", .width = 1280, .height = 720},
            .tickRateHz = 60.0,
            .assetRoot = "assets",
            .enableDevUi = true,
            .devUiIniPath = roboslop::configDir() / "gorden.imgui.ini",
            .closeOnEscape = false,
            .onSetup = [initial, document = *scene, scenePathText = scenePath.string()](
                           roboslop::World& world, roboslop::AssetCache& assets
                       ) -> roboslop::Result<void> {
                const auto cameraEntity = world.create();
                world.emplace<roboslop::Transform>(cameraEntity, document.camera);
                world.emplace<roboslop::Camera>(
                    cameraEntity, roboslop::Camera{.projection = roboslop::Perspective{}}
                );
                world.emplace<roboslop::ActiveCamera>(cameraEntity);
                world.emplace<roboslop::AudioListener>(cameraEntity);
                world.emplace<gorden::OrbitCamera>(cameraEntity);
                world.registry().ctx().emplace<gorden::PlayerControls>();

                auto& runtime = world.registry().ctx().emplace<roboslop::SceneRuntime>();
                if (auto loaded = runtime.replace(world, assets, document, true); !loaded) {
                    return loaded;
                }
                world.forEach<roboslop::SceneIdentity>([&world](auto entity, const auto& identity) {
                    world.emplace<gorden::Named>(entity, gorden::Named{.name = identity.name});
                });

                // Gorden keeps its existing kinematic identity and save origin.
                const auto robot = world.create();
                world.emplace<roboslop::Transform>(
                    robot, roboslop::Transform{.position = {2.0F, 0.0F, 4.0F}}
                );
                auto robotModel = gorden::loadRobotModel(runtime, assets);
                if (!robotModel) {
                    return std::unexpected(robotModel.error());
                }
                world.emplace<roboslop::ModelInstance>(robot, std::move(*robotModel));
                world.emplace<gorden::Named>(
                    robot, gorden::Named{.name = initial.settings.robotName}
                );
                world.emplace<gorden::Robot>(robot);
                world.emplace<gorden::RobotMotion>(robot, gorden::RobotMotion{.speed = 2.5F});

                // The model borrows GPU resources from the scene runtime,
                // which keeps its model cache alive across save/load.
                const auto player = world.create();
                world.emplace<roboslop::Transform>(
                    player, roboslop::Transform{.position = {0.0F, 0.4F, 4.0F}}
                );
                auto playerModel = gorden::loadPlayerModel(runtime, assets);
                if (!playerModel) {
                    return std::unexpected(playerModel.error());
                }
                world.emplace<roboslop::ModelInstance>(player, std::move(*playerModel));
                world.emplace<gorden::Named>(
                    player, gorden::Named{.name = initial.settings.playerName}
                );
                world.emplace<gorden::Player>(player);
                gorden::followPlayer(
                    world.get<gorden::OrbitCamera>(cameraEntity),
                    world.get<roboslop::Transform>(player),
                    world.get<roboslop::Transform>(cameraEntity),
                    *world.registry().ctx().get<roboslop::JoltWorld*>()
                );

                auto& ctx = world.registry().ctx();
                gorden::BrainConfig brainCfg;
                brainCfg.robotName = initial.settings.robotName;
                brainCfg.playerName = initial.settings.playerName;
                ctx.emplace<gorden::AgentBrain>(makeProvider(), brainCfg, robot, player);
                ctx.emplace<RobotPanelState>();
                ctx.emplace<AgentLogState>();
                auto& saveState = ctx.emplace<SaveState>(
                    SaveState{.scenePath = scenePathText, .document = document}
                );
                copyToBuffer(saveState.slotBuf, saveState.slot);
                auto& st = ctx.emplace<SettingsState>(initial);

                if (auto* ui = roboslop::devUi(world); ui != nullptr) {
                    ui->registerWindow(
                        roboslop::DevWindow{
                            .id = "robot",
                            .title = "Robot",
                            .draw =
                                [&world]() {
                                    auto& c = world.registry().ctx();
                                    drawRobotPanel(
                                        world, c.get<gorden::AgentBrain>(), c.get<RobotPanelState>()
                                    );
                                },
                            .visible = true,
                        }
                    );
                    ui->registerWindow(
                        roboslop::DevWindow{
                            .id = "agentLog",
                            .title = "Agent log",
                            .draw =
                                [&world]() {
                                    auto& c = world.registry().ctx();
                                    drawAgentLogPanel(
                                        c.get<gorden::AgentBrain>(), c.get<AgentLogState>()
                                    );
                                },
                            .visible = true,
                        }
                    );
                    ui->registerWindow(
                        roboslop::DevWindow{
                            .id = "settings",
                            .title = "Settings",
                            .draw =
                                [&world]() {
                                    auto& c = world.registry().ctx();
                                    drawSettingsPanel(
                                        world, c.get<SettingsState>(), c.get<gorden::AgentBrain>()
                                    );
                                },
                            .visible = false,
                        }
                    );
                    auto& fs = ctx.emplace<roboslop::Vfs>();
                    mountGordenFiles(world, fs);
                    auto& shell = ctx.emplace<roboslop::Shell>(
                        fs,
                        roboslop::ShellConfig{
                            .user = st.settings.playerName,
                            .host = "gorden",
                            .home = "/home/" + st.settings.playerName,
                        }
                    );
                    // The robot's own shell can save and load: `save`,
                    // `load` and an optional slot name, the same two
                    // requests the Settings buttons raise.
                    for (const auto& [name, request] :
                         {std::pair{"save", SaveState::Request::Save},
                          std::pair{"load", SaveState::Request::Load}}) {
                        shell.registerCommand(
                            name,
                            std::string{name} + " [slot] - " +
                                (request == SaveState::Request::Save ? "write" : "read") +
                                " a save game",
                            [&world,
                             request](roboslop::CommandContext& c) -> roboslop::ShellResult {
                                auto& st = world.registry().ctx().get<SaveState>();
                                if (c.args.size() > 2) {
                                    return {
                                        .output = "usage: " + c.args[0] + " [slot]\n", .status = 1
                                    };
                                }
                                if (c.args.size() == 2) {
                                    st.slot = c.args[1];
                                    copyToBuffer(st.slotBuf, st.slot);
                                }
                                st.request = request;
                                // The work happens in the render pass, so
                                // the result shows up in the next output.
                                return {
                                    .output = "requested; see the Settings window\n", .status = 0
                                };
                            }
                        );
                    }
                    ctx.emplace<roboslop::TerminalWindow>(shell);
                    ui->registerWindow(
                        roboslop::DevWindow{
                            .id = "terminal",
                            .title = "Terminal",
                            .draw =
                                [&world]() {
                                    world.registry().ctx().get<roboslop::TerminalWindow>().draw();
                                },
                            .visible = true,
                        }
                    );

                    std::vector<roboslop::WindowVisibility> saved;
                    saved.reserve(st.settings.windows.size());
                    for (const auto& [id, visible] : st.settings.windows) {
                        saved.push_back({.id = id, .visible = visible});
                    }
                    ui->applyVisibility(saved);
                    for (const auto& v : ui->visibility()) {
                        st.lastVisibility[v.id] = v.visible;
                    }
                }

                // Stash light uniform handles on the world so the pass
                // record callback can find them each frame without
                // capturing app-locals.
                world.registry().ctx().emplace<roboslop::LightUniforms>(roboslop::LightUniforms{
                    .dir = assets.uniform("u_lightDir", bgfx::UniformType::Vec4),
                    .color = assets.uniform("u_lightColor", bgfx::UniformType::Vec4),
                    .pointPosition =
                        assets.uniform("u_pointLightPosition", bgfx::UniformType::Vec4),
                    .pointColor = assets.uniform("u_pointLightColor", bgfx::UniformType::Vec4),
                });
                return {};
            },
            .onBuildGraphs =
                [](roboslop::SystemGraph& fixed,
                   roboslop::RenderGraph& render,
                   roboslop::FrameArena& arena) {
                    roboslop::registerPhysicsSystems(fixed);
                    fixed.add({
                        .name = "playerMovement",
                        .reads = {"agent"},
                        .writes = {"transforms", "physicsState", "input"},
                        .run = [](roboslop::SystemCtx& c) {
                            auto& ctx = c.world->registry().ctx();
                            auto& physics = *ctx.get<roboslop::JoltWorld*>();
                            const auto in = gorden::readPlayerInput(
                                *c.input, ctx.get<gorden::PlayerControls>()
                            );
                            const auto entity = ctx.get<gorden::AgentBrain>().playerEntity();
                            auto& transform = c.world->get<roboslop::Transform>(entity);
                            auto& player = c.world->get<gorden::Player>(entity);
                            c.world->forEach<gorden::OrbitCamera, roboslop::Transform>(
                                [&](auto& orbit, auto& camera) {
                                    gorden::turnCamera(orbit, in, static_cast<float>(c.dt));
                                    gorden::movePlayer(
                                        player,
                                        transform,
                                        physics,
                                        gorden::playerVelocity(orbit, in.move),
                                        static_cast<float>(c.dt)
                                    );
                                    gorden::followPlayer(orbit, transform, camera, physics);
                                }
                            );
                        },
                    });
                    fixed.add({
                        .name = "robotLocomotion",
                        .reads = {},
                        .writes = {"transforms"},
                        .run = [](roboslop::SystemCtx& c) { gorden::robotLocomotion(c); },
                    });
                    fixed.add({
                        .name = "agentPump",
                        .reads = {"transforms"},
                        .writes = {"agent"},
                        .run = [](roboslop::SystemCtx& c) {
                            c.world->registry().ctx().get<gorden::AgentBrain>().pump(
                                *c.world, c.dt
                            );
                        },
                    });
                    roboslop::registerAudioSystems(fixed);
                    render.add({
                        .name = "main",
                        .reads = {"transforms", "lights"},
                        .writes = {"framebuffer"},
                        .record = [&arena](roboslop::PassCtx& c) {
                            roboslop::applyActiveCamera(
                                *c.world, c.viewId, c.viewportW, c.viewportH
                            );
                            const auto& lu =
                                c.world->registry().ctx().get<roboslop::LightUniforms>();
                            roboslop::uploadLights(*c.world, lu);
                            auto draws = roboslop::collectMeshDraws(*c.world, arena, c.viewId);
                            roboslop::sortDraws(draws);
                            roboslop::submitDraws(draws);
                        },
                    });
                    render.add({
                        .name = "robotChat",
                        .reads = {"framebuffer"},
                        .writes = {"framebuffer"},
                        .record = [](roboslop::PassCtx& c) {
                            auto* ui = roboslop::devUi(*c.world);
                            if (ui == nullptr) {
                                return;
                            }
                            auto& ctx = c.world->registry().ctx();
                            auto& save = ctx.get<SaveState>();
                            if (save.request != SaveState::Request::None) {
                                const auto request =
                                    std::exchange(save.request, SaveState::Request::None);
                                save.status =
                                    performSaveRequest(*c.world, *c.assets, save, request);
                                spdlog::info("gorden: {}", save.status);
                            }
                            ui->beginFrame();
                            ui->drawWindows();
                            auto& controls = ctx.get<gorden::PlayerControls>();
                            controls.uiMouse = ui->wantCaptureMouse();
                            controls.uiKeyboard = ui->wantCaptureKeyboard();
                            ui->endFrame(c.viewId);

                            // Persist window visibility when it changes
                            // (menu or close button; F1 does not count).
                            auto& settings = ctx.get<SettingsState>();
                            std::map<std::string, bool> now;
                            for (const auto& v : ui->visibility()) {
                                now[v.id] = v.visible;
                            }
                            if (now != settings.lastVisibility) {
                                settings.lastVisibility = now;
                                settings.settings.windows = now;
                                saveSettingsNow(settings);
                            }
                        },
                    });
                },
        }
    );

    if (!app) {
        std::println(stderr, "app init failed: {}", app.error().message);
        return 1;
    }

    const auto result = app->run();
    if (!result) {
        std::println(
            stderr, "app exited with error: {} ({})", result.error().message, result.error().context
        );
        return 1;
    }
    return 0;
}
