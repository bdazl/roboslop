// Display-dependent integration test, deliberately separate from headless CTest.
import editor.model;
import roboslop.app;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.lighting;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.transform;
import roboslop.sched;
#include <bgfx/bgfx.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <print>
#include <string>

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        std::println(stderr, "Usage: editor_runtime_smoke <temporary-output-directory>");
        return 1;
    }
    auto source = roboslop::loadScene("assets/scenes/room.json");
    if (!source) {
        std::println(stderr, "{}", source.error().context);
        return 1;
    }
    const std::filesystem::path output = std::filesystem::path(argv[1]) / "roundtrip.json";
    editor::History history;
    history.reset(*source);
    const auto original = history.document;
    history.document.objects.push_back(
        {.id = editor::nextId(history.document),
         .name = "Smoke test cube",
         .transform = {.position = {0, 8, 0}},
         .body = "dynamic"}
    );
    history.checkpoint(original);
    const auto expected = roboslop::sceneToJson(history.document);
    if (!history.undo() || !history.redo() || roboslop::sceneToJson(history.document) != expected) {
        return 1;
    }
    if (!roboslop::saveScene(output, history.document)) {
        return 1;
    }
    const auto loaded = roboslop::loadScene(output);
    if (!loaded || roboslop::sceneToJson(*loaded) != expected) {
        return 1;
    }
    int frame = 0;
    bool failed = false;
    const auto& document = *loaded;
    const auto probe = document.objects.back().id;
    roboslop::LightUniforms uniforms;
    auto app = roboslop::App::make(
        {.window = {.title = "roboslop-editor-smoke", .width = 800, .height = 600},
         .assetRoot = "assets",
         .onSetup = [&](roboslop::World& world,
                        roboslop::AssetCache& assets) -> roboslop::Result<void> {
             const auto camera = world.create();
             world.emplace<roboslop::Transform>(camera, document.camera);
             world.emplace<roboslop::Camera>(
                 camera, roboslop::Camera{.projection = roboslop::Perspective{}}
             );
             world.emplace<roboslop::ActiveCamera>(camera);
             uniforms = {
                 .dir = assets.uniform("u_lightDir", bgfx::UniformType::Vec4),
                 .color = assets.uniform("u_lightColor", bgfx::UniformType::Vec4),
                 .pointPosition = assets.uniform("u_pointLightPosition", bgfx::UniformType::Vec4),
                 .pointColor = assets.uniform("u_pointLightColor", bgfx::UniformType::Vec4)
             };
             return world.registry().ctx().emplace<roboslop::SceneRuntime>().replace(
                 world, assets, document, false
             );
         },
         .onBuildGraphs =
             [&](roboslop::SystemGraph&,
                 roboslop::RenderGraph& render,
                 roboslop::FrameArena& arena) {
                 render.add(
                     {.name = "smoke",
                      .reads = {},
                      .writes = {"framebuffer"},
                      .record = [&](roboslop::PassCtx& c) {
                          auto& world = *c.world;
                          auto& runtime = world.registry().ctx().get<roboslop::SceneRuntime>();
                          const int phase = frame % 12;
                          if (phase == 0 || phase == 11) {
                              auto result = runtime.replace(world, *c.assets, document, phase == 0);
                              if (!result) {
                                  failed = true;
                              }
                          }
                          if (phase < 10) {
                              roboslop::SystemCtx step{.world = &world, .dt = 1.0 / 60.0};
                              roboslop::physicsSpawn(step);
                              for (int i = 0; i < 6; ++i) {
                                  roboslop::physicsStep(step);
                              }
                              roboslop::syncPhysicsToTransform(step);
                          }
                          bool found = false;
                          world.forEach<roboslop::SceneIdentity, roboslop::Transform>(
                              [&](const auto& id, const auto& t) {
                                  if (id.id == probe) {
                                      found = true;
                                      if (phase == 10 && t.position.y >= 7) {
                                          failed = true;
                                      }
                                      if (phase == 11 && t.position.y != 8) {
                                          failed = true;
                                      }
                                  }
                              }
                          );
                          failed |= !found || roboslop::sceneToJson(document) != expected;
                          roboslop::applyActiveCamera(world, c.viewId, c.viewportW, c.viewportH);
                          roboslop::uploadLights(world, uniforms);
                          auto draws = roboslop::collectMeshDraws(world, arena, c.viewId);
                          roboslop::sortDraws(draws);
                          roboslop::submitDraws(draws);
                          if (++frame >= 120 || failed) {
                              roboslop::requestAppClose(world);
                          }
                      }}
                 );
             }}
    );
    if (!app) {
        std::println(stderr, "{}", app.error().context);
        return 1;
    }
    const auto result = app->run();
    failed |= !result || frame < 120;
    std::println(
        "Editor runtime smoke: {} ({} frames, 10 Play/Stop cycles, saved {})",
        failed ? "FAIL" : "PASS",
        frame,
        output.string()
    );
    return failed ? 1 : 0;
}
