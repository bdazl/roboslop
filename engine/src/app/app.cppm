module;

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

export module roboslop.app;

import roboslop.audio;
import roboslop.audio.device;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.platform.input;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.context;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.sched;
import roboslop.time.clock;
import roboslop.time.frame_stats;
import roboslop.app.benchmark;
import roboslop.ui;
import roboslop.ui.perf_window;

namespace roboslop {

export struct AppExitRequest {};

// Rendering and input continue while the fixed simulation is paused.
export struct AppSimulationState {
    bool paused = false;
};

export auto requestAppClose(World& world) -> void {
    world.registry().ctx().emplace<AppExitRequest>();
}

// Game-supplied hooks.
//
//   onSetup        — once after init, before the loop. Receives the
//                    World and the App-owned AssetCache so game code
//                    can request programs without managing handle
//                    lifetimes.
//   onFrame        — once per frame after input polling, before fixed updates.
//   onBuildGraphs  — once after onSetup, before the loop. Receives the
//                    fixed-step SystemGraph, the render-pass
//                    RenderGraph, and the per-frame FrameArena
//                    (captured by reference into pass record
//                    callbacks). The engine compiles the SystemGraph
//                    after this returns; do not call add() once the
//                    main loop has started.
//
// There are no per-frame onFixedUpdate / onRender callbacks any more —
// the same logic lives as named systems / passes in the two graphs.
export struct AppConfig {
    WindowConfig window;
    double tickRateHz = 60.0;
    std::filesystem::path assetRoot = ".";

    // Default 4 MiB — ~32 768 DrawItems at 128 B each, comfortable
    // overhead for any milestone's worst-case frame.
    std::size_t frameArenaBytes = std::size_t{4U} * 1024U * 1024U;

    // 0 selects defaultWorkerCount() (hardware concurrency - 1,
    // clamped [1, 8]).
    unsigned workerThreads = 0;

    // Create a DevUi (Dear ImGui) and install it into the world so
    // passes can reach it via devUi(world). Ignored with a warning when
    // the engine was configured with ROBOSLOP_DEV_UI=OFF. F1 toggles
    // the overlay at runtime.
    bool enableDevUi = false;
    // Gameplay ImGui remains available independently of developer tools.
    bool enableGameUi = false;
    // ImGui layout persistence; empty keeps the layout in memory.
    std::filesystem::path devUiIniPath{};
    float uiFontSize = 13.0F;
    bool closeOnEscape = true;
    // Optional veto used by document editors to offer Save/Discard/Cancel.
    std::function<bool(World&)> onCloseRequested{};

    std::function<void(World&, Input&)> onFrame{};
    std::function<Result<void>(World&, AssetCache&)> onSetup;
    std::function<void(SystemGraph&, RenderGraph&, FrameArena&)> onBuildGraphs;
};

// The engine entry point. A game constructs an App via make(), then calls
// run() to drive the semi-fixed timestep loop until the window closes.
// App owns the window, render context, world, clock, scheduler, system
// and render graphs, and the per-frame arena; the onSetup +
// onBuildGraphs callbacks in AppConfig are how gameplay code
// participates.
export class App {
  public:
    [[nodiscard]] static auto make(AppConfig cfg) -> Result<App> {
        auto window = Window::make(cfg.window);
        if (!window) {
            return std::unexpected(window.error());
        }
        const auto bench = benchmarkFromEnv();
        RenderConfig renderCfg{};
        if (bench.frames > 0) {
            renderCfg = withoutVsync(renderCfg);
        }
        auto render = RenderContext::make(*window, renderCfg);
        if (!render) {
            return std::unexpected(render.error());
        }
        AssetCache assets{cfg.assetRoot};
        std::optional<DevUi> ui;
#if !ROBOSLOP_DEV_UI
        if (cfg.enableDevUi) {
            spdlog::warn("roboslop: enableDevUi requested but ROBOSLOP_DEV_UI is OFF");
            cfg.enableDevUi = false;
        }
#endif
        if (cfg.enableGameUi || cfg.enableDevUi) {
            auto made = DevUi::make(
                *window,
                assets,
                DevUiConfig{.iniPath = cfg.devUiIniPath, .fontSize = cfg.uiFontSize}
            );
            if (!made) {
                return std::unexpected(made.error());
            }
            ui.emplace(std::move(*made));
        }
        const unsigned workers = cfg.workerThreads == 0 ? defaultWorkerCount() : cfg.workerThreads;
        FrameArena arena{cfg.frameArenaBytes};
        Scheduler scheduler{workers};
        JoltWorld physics = JoltWorld::make();
        auto audio = AudioDevice::make();
        if (!audio) {
            return std::unexpected(audio.error());
        }
        return App{
            std::move(*window),
            std::move(*render),
            std::move(assets),
            std::move(ui),
            std::move(physics),
            std::move(*audio),
            std::move(scheduler),
            std::move(arena),
            bench,
            std::move(cfg)
        };
    }

    App(const App&) = delete;
    auto operator=(const App&) -> App& = delete;
    App(App&&) noexcept = default;
    auto operator=(App&&) noexcept -> App& = default;
    ~App() = default;

    auto run() -> Result<void> {
        window.setResizeCallback([this](int w, int h) { render.resize(w, h); });
        world.registry().ctx().emplace<AppSimulationState>();
        installJoltWorld(world, physics);
        installAudioDevice(world, audio);
        if (ui) {
            installDevUi(world, *ui);
        }

        if (ui && cfg.enableDevUi) {
            ui->registerWindow(makePerfWindow(
                stats,
                RenderContext::multiThreaded() ? "bgfx multi-threaded" : "bgfx single-threaded"
            ));
        }

        if (cfg.onSetup) {
            auto setupResult = cfg.onSetup(world, assets);
            if (!setupResult) {
                return std::unexpected(setupResult.error());
            }
        }

        if (cfg.onBuildGraphs) {
            cfg.onBuildGraphs(fixedGraph, renderGraph, arena);
        }
        fixedGraph.compile();

        spdlog::info(
            "roboslop: entering main loop (workers={}, fixedSystems={}, passes={})",
            scheduler.workerCount(),
            fixedGraph.size(),
            renderGraph.size()
        );
        clock.reset();

        const bool benchmarking = bench.frames > 0;
        if (benchmarking) {
            spdlog::info(
                "roboslop: benchmark {} frames ({} warm-up), fixed step, vsync off",
                bench.frames,
                bench.warmupFrames
            );
        }
        std::size_t frameIndex = 0;

        while (true) {
            const auto frameStart = std::chrono::steady_clock::now();
            arena.reset();
            pollWindowEvents();
            input.beginFrame(capturePlatformInput(window));

            if (cfg.closeOnEscape && input.keyPressed(Key::Escape)) {
                window.requestClose();
            }
            if (world.registry().ctx().contains<AppExitRequest>()) {
                world.registry().ctx().erase<AppExitRequest>();
                window.requestClose();
            }
            if (window.shouldClose()) {
                if (!cfg.onCloseRequested || cfg.onCloseRequested(world)) {
                    break;
                }
                window.cancelClose();
            }
            if (ui && cfg.enableDevUi && input.keyPressed(Key::F1)) {
                ui->toggleEnabled();
            }

            if (cfg.onFrame) {
                cfg.onFrame(world, input);
            }

            // A benchmark must not let a slow frame feed back into the
            // simulation load, or the measurement measures itself.
            const double wallDt = clock.tickFrame();
            const double dt = benchmarking ? ticker.fixedDelta() : wallDt;
            const int steps = ticker.advance(dt);
            const auto fixedStart = std::chrono::steady_clock::now();
            for (int i = 0; i < steps && !world.registry().ctx().get<AppSimulationState>().paused;
                 ++i) {
                SystemCtx ctx{
                    .world = &world,
                    .input = &input,
                    .dt = ticker.fixedDelta(),
                    .alpha = 0.0,
                    .stage = FrameStage::FixedUpdate,
                };
                scheduler.run(fixedGraph, ctx);
            }
            // Systems request cursor capture from scheduler workers; the
            // window may only be touched here, on its own thread.
            input.applyCursorRequest(window);

            const auto renderStart = std::chrono::steady_clock::now();
            RenderContext::beginFrame();
            renderGraph.execute(world, render, assets);
            RenderContext::endFrame();
            const auto frameEnd = std::chrono::steady_clock::now();

            const auto gpu = RenderContext::gpuStats();
            stats.push({
                .cpuFrameMs = millis(frameStart, frameEnd),
                .fixedMs = millis(fixedStart, renderStart),
                .renderMs = millis(renderStart, frameEnd),
                .gpuMs = gpu.gpuMs,
                .waitSubmitMs = gpu.waitSubmitMs,
                .waitRenderMs = gpu.waitRenderMs,
                .drawCalls = gpu.drawCalls,
                .backbufferWidth = gpu.backbufferWidth,
                .backbufferHeight = gpu.backbufferHeight,
            });

            if (benchmarking) {
                ++frameIndex;
                if (frameIndex == bench.warmupFrames) {
                    stats.clear();
                }
                if (frameIndex >= bench.warmupFrames + bench.frames) {
                    reportBenchmark(stats, bench, RenderContext::multiThreaded());
                    break;
                }
            }
        }

        spdlog::info("roboslop: main loop exited");
        return {};
    }

    // Recent frame timings, for the dev-UI overlay and the benchmark
    // summary. Written once per frame by run().
    [[nodiscard]] auto frameStats() const noexcept -> const FrameStats& {
        return stats;
    }

  private:
    using SteadyPoint = std::chrono::steady_clock::time_point;

    [[nodiscard]] static auto millis(SteadyPoint from, SteadyPoint to) noexcept -> double {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    App(Window window,
        RenderContext render,
        AssetCache assets,
        std::optional<DevUi> ui,
        JoltWorld physics,
        AudioDevice audio,
        Scheduler scheduler,
        FrameArena arena,
        BenchmarkConfig bench,
        AppConfig cfg) noexcept
        : window(std::move(window)), render(std::move(render)), assets(std::move(assets)),
          ui(std::move(ui)), physics(std::move(physics)), audio(std::move(audio)),
          ticker(cfg.tickRateHz), scheduler(std::move(scheduler)), arena(std::move(arena)),
          // A benchmark summarises every measured frame, so the ring has
          // to hold the whole run rather than the last few seconds.
          stats(bench.frames > 0 ? bench.frames : FrameStats::DefaultCapacity),
          bench(std::move(bench)), cfg(std::move(cfg)) {}

    // Member order is destruction-critical: assets must outlive any
    // entity that stores its handles (world) and must die before bgfx
    // shuts down (render). Declared order = construction order =
    // reverse destruction order. input stores only the raw GLFWwindow
    // handle, which is stable across Window moves, so it carries no
    // destruction dependency of its own. ui borrows its program from
    // assets and owns bgfx resources, so it sits right after assets:
    // destroyed before the cache and before bgfx shuts down, after the
    // world (whose ctx holds a DevUi*). physics is declared before
    // world so world destructs first — clearing entt's ctx<JoltWorld*>
    // entry before the JoltWorld itself tears down Jolt globals.
    // fixedGraph and renderGraph hold lambdas captured from
    // onBuildGraphs that may reference arena and world by pointer; both
    // die before those targets do.
    Window window;
    RenderContext render;
    Input input;
    AssetCache assets;
    std::optional<DevUi> ui;
    JoltWorld physics;
    AudioDevice audio;
    World world;
    Clock clock;
    FixedTimestep ticker;
    Scheduler scheduler;
    FrameArena arena;
    SystemGraph fixedGraph;
    RenderGraph renderGraph;
    FrameStats stats;
    BenchmarkConfig bench;
    AppConfig cfg;
};

} // namespace roboslop
