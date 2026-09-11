// Display-dependent integration test, deliberately separate from headless CTest:
// applySave rebuilds the scene through SceneRuntime, which needs a real
// render context. Run it from the build directory with a temporary output
// directory as its only argument.
import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import gorden.first_room;
import gorden.save;
import gorden.player;
import gorden.player_visual;
import gorden.robot_visual;
import roboslop.render.model;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.app;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.lighting;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.savegame;
import roboslop.scene.transform;
import roboslop.sched;

#include <bgfx/bgfx.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <expected>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <vector>

namespace {

constexpr const char* ScenePath = "assets/scenes/room.json";
constexpr float MovedX = -7.5F;

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        std::println(stderr, "Usage: gorden_save_smoke <temporary-output-directory>");
        return 1;
    }
    const std::filesystem::path savePath = std::filesystem::path(argv[1]) / "smoke.json";
    auto scene = roboslop::loadScene(ScenePath);
    if (!scene) {
        std::println(stderr, "{}", scene.error().context);
        return 1;
    }

    int frame = 0;
    bool failed = false;
    float movementStartX = 0.0F;
    bgfx::VertexBufferHandle robotBuffer{bgfx::kInvalidHandle};
    bgfx::VertexBufferHandle playerBuffer{bgfx::kInvalidHandle};
    std::string failure;
    const auto document = *scene;
    const auto probe = document.objects.front().id;
    roboslop::LightUniforms uniforms;
    auto app = roboslop::App::make(
        {.window = {.title = "gorden-save-smoke", .width = 800, .height = 600},
         .assetRoot = "assets",
         .onSetup = [&](roboslop::World& world,
                        roboslop::AssetCache& assets) -> roboslop::Result<void> {
             const auto camera = world.create();
             world.emplace<roboslop::Transform>(camera, document.camera);
             world.emplace<roboslop::Camera>(
                 camera, roboslop::Camera{.projection = roboslop::Perspective{}}
             );
             world.emplace<roboslop::ActiveCamera>(camera);
             const auto player = world.create();
             world.emplace<roboslop::Transform>(
                 player, roboslop::Transform{.position = {0.0F, 0.4F, 4.0F}}
             );
             world.emplace<gorden::Player>(player);
             world.emplace<gorden::Named>(player, gorden::Named{.name = "Player"});
             uniforms = {
                 .dir = assets.uniform("u_lightDir", bgfx::UniformType::Vec4),
                 .color = assets.uniform("u_lightColor", bgfx::UniformType::Vec4),
                 .pointPosition = assets.uniform("u_pointLightPosition", bgfx::UniformType::Vec4),
                 .pointColor = assets.uniform("u_pointLightColor", bgfx::UniformType::Vec4)
             };
             auto& runtime = world.registry().ctx().emplace<roboslop::SceneRuntime>();
             if (auto loaded = runtime.replace(world, assets, document, true); !loaded) {
                 return loaded;
             }
             gorden::attachSceneSemantics(world);

             auto model = gorden::loadPlayerModel(runtime, assets);
             if (!model) {
                 return std::unexpected(model.error());
             }
             world.emplace<roboslop::ModelInstance>(player, std::move(*model));

             const auto robot = world.create();
             world.emplace<roboslop::Transform>(
                 robot, roboslop::Transform{.position = {2.0F, 0.0F, 4.0F}}
             );
             auto robotModel = gorden::loadRobotModel(runtime, assets);
             if (!robotModel) {
                 return std::unexpected(robotModel.error());
             }
             world.emplace<roboslop::ModelInstance>(robot, std::move(*robotModel));
             world.emplace<gorden::Named>(robot, gorden::Named{.name = "Gorden"});
             world.emplace<gorden::Robot>(robot);
             world.emplace<gorden::RobotMotion>(robot, gorden::RobotMotion{.speed = 2.5F});

             world.registry().ctx().emplace<gorden::FirstRoomProgress>();
             auto& brain = world.registry().ctx().emplace<gorden::AgentBrain>(
                 std::make_unique<roboslop::ScriptedProvider>(
                     std::vector<roboslop::ChatResponse>{}
                 ),
                 gorden::BrainConfig{},
                 robot,
                 player
             );
             gorden::AgentMemory memory;
             memory.remember("the crate hides a key", 1, 10.0);
             memory.setGoal("open the crate", 11.0);
             brain.setMemory(std::move(memory), 12.0);
             return {};
         },
         .onBuildGraphs =
             [&](roboslop::SystemGraph& fixed,
                 roboslop::RenderGraph& render,
                 roboslop::FrameArena& arena) {
                 roboslop::registerPhysicsSystems(fixed);
                 fixed.add({
                     .name = "player",
                     .reads = {},
                     .writes = {"transforms", "physicsState"},
                     .run = [&frame](roboslop::SystemCtx& c) {
                         auto& world = *c.world;
                         const auto entity =
                             world.registry().ctx().get<gorden::AgentBrain>().playerEntity();
                         gorden::movePlayer(
                             world.get<gorden::Player>(entity),
                             world.get<roboslop::Transform>(entity),
                             *world.registry().ctx().get<roboslop::JoltWorld*>(),
                             frame >= 60 && frame < 90 ? glm::vec3{2.0F, 0.0F, 0.0F}
                                                       : glm::vec3{0.0F},
                             static_cast<float>(c.dt)
                         );
                     },
                 });
                 fixed.add({
                     .name = "robot",
                     .reads = {},
                     .writes = {"transforms"},
                     .run = [](roboslop::SystemCtx& c) { gorden::robotLocomotion(c); },
                 });
                 render.add(
                     {.name = "smoke",
                      .reads = {},
                      .writes = {"framebuffer"},
                      .record = [&](roboslop::PassCtx& c) {
                          auto& world = *c.world;
                          auto& brain = world.registry().ctx().get<gorden::AgentBrain>();
                          auto& runtime = world.registry().ctx().get<roboslop::SceneRuntime>();
                          auto& progress = world.registry().ctx().get<gorden::FirstRoomProgress>();
                          const auto doorState = [&] {
                              const auto door = gorden::findSceneEntity(world, gorden::ExitDoorId);
                              struct {
                                  float z;
                                  bool body;
                                  std::string state;
                              } out{
                                  world.get<roboslop::Transform>(*door).position.z,
                                  world.has<roboslop::RigidBody>(*door) ||
                                      world.has<roboslop::BodyDesc>(*door),
                                  world.get<gorden::Inspectable>(*door).state,
                              };
                              return out;
                          };
                          const auto fail = [&](std::string why) {
                              failed = true;
                              failure = std::move(why);
                          };

                          auto& playerTransform =
                              world.get<roboslop::Transform>(brain.playerEntity());
                          const auto& playerModel =
                              world.get<roboslop::ModelInstance>(brain.playerEntity());
                          if (playerModel.parts.empty()) {
                              fail("player model has no draw parts");
                          } else if (frame == 0) {
                              playerBuffer = playerModel.parts.front().mesh.vb;
                          } else if (playerModel.parts.front().mesh.vb.idx != playerBuffer.idx) {
                              fail("scene replacement invalidated the player model");
                          }

                          const auto& robotModel =
                              world.get<roboslop::ModelInstance>(brain.robotEntity());
                          if (robotModel.parts.empty()) {
                              fail("robot model has no draw parts");
                          } else if (frame == 0) {
                              robotBuffer = robotModel.parts.front().mesh.vb;
                          } else if (robotModel.parts.front().mesh.vb.idx != robotBuffer.idx) {
                              fail("scene replacement invalidated the robot model");
                          }
                          if (frame == 7) {
                              auto& robotTransform =
                                  world.get<roboslop::Transform>(brain.robotEntity());
                              robotTransform.position = {2.0F, 0.0F, 3.0F};
                              world.get<gorden::RobotMotion>(brain.robotEntity()).target =
                                  glm::vec3{2.0F, 0.0F, 5.0F};
                          } else if (frame == 119) {
                              const auto& robotTransform =
                                  world.get<roboslop::Transform>(brain.robotEntity());
                              const auto facing =
                                  robotTransform.rotation * glm::vec3{0.0F, 0.0F, -1.0F};
                              if (std::abs(robotTransform.position.z - 5.0F) > 0.01F ||
                                  facing.z < 0.99F || robotTransform.position.y != 0.0F) {
                                  fail("robot did not arrive facing forward at its saved height");
                              }
                          }

                          if (frame == 1) {
                              // Move the robot away from where the save
                              // will remember it, then write the save.
                              world.get<roboslop::Transform>(brain.robotEntity()).position.x =
                                  MovedX;
                              // Solve the room, so the save carries an open door.
                              const auto bay = gorden::findSceneEntity(world, gorden::ConduitBayId);
                              world.get<gorden::Inspectable>(*bay).inspected = true;
                              progress.interlockVerified = true;
                              if (!gorden::openExitDoor(world, progress, brain)) {
                                  fail("the verified interlock did not open the door");
                              }
                              const auto save =
                                  gorden::captureSave(world, brain, progress, "scenes/room.json");
                              if (!roboslop::saveSaveGame(savePath, save)) {
                                  fail("saveSaveGame failed");
                              }
                          } else if (frame == 2) {
                              // Now change the world and forget everything.
                              world.get<roboslop::Transform>(brain.robotEntity()).position.x = 0.0F;
                              brain.setMemory(gorden::AgentMemory{}, 0.0);
                              world.get<roboslop::Transform>(brain.playerEntity()).position.x =
                                  5.0F;
                              world.get<gorden::Player>(brain.playerEntity()).reset();
                              progress = {};
                          } else if (frame == 3) {
                              auto loaded = roboslop::loadSaveGame(savePath);
                              if (!loaded) {
                                  fail("loadSaveGame failed: " + loaded.error().context);
                              } else if (
                                  auto applied = gorden::applySave(
                                      world, *c.assets, runtime, document, brain, progress, *loaded
                                  );
                                  !applied
                              ) {
                                  fail("applySave failed: " + applied.error().context);
                              }
                          } else if (frame == 4) {
                              if (world.get<roboslop::Transform>(brain.robotEntity()).position.x !=
                                  MovedX) {
                                  fail("the robot did not return to its saved position");
                              }
                              // One character step after the load may leave float noise.
                              if (std::abs(world.get<roboslop::Transform>(brain.playerEntity())
                                               .position.x) > 0.001F) {
                                  fail(
                                      "the player controller did not return to its saved position"
                                  );
                              }
                              if (brain.memory().recall("crate key", 5).size() != 1 ||
                                  brain.memory().activeGoals().size() != 1 ||
                                  brain.simTime() != 12.0) {
                                  fail("the memory did not come back");
                              }
                              bool found = false;
                              world.forEach<roboslop::SceneIdentity, gorden::Named>(
                                  [&](const auto& identity, const auto& named) {
                                      found |= identity.id == probe && !named.name.empty();
                                  }
                              );
                              if (!found) {
                                  fail("the rebuilt scene lost its perception names");
                              }
                              const auto door = doorState();
                              const auto bay = gorden::findSceneEntity(world, gorden::ConduitBayId);
                              if (!progress.doorOpen || !progress.interlockVerified || door.body ||
                                  std::abs(door.z + 2.4F) > 0.001F || door.state != "open" ||
                                  !world.get<gorden::Inspectable>(*bay).inspected) {
                                  fail("the open exit door did not come back");
                              }
                          } else if (frame == 5) {
                              auto legacy =
                                  gorden::captureSave(world, brain, progress, "scenes/room.json");
                              legacy.app["version"] = 1;
                              legacy.app["player"]["scale"] = {0.7F, 1.8F, 0.7F};
                              if (auto applied = gorden::applySave(
                                      world, *c.assets, runtime, document, brain, progress, legacy
                                  );
                                  !applied) {
                                  fail("legacy player save failed");
                              }
                          } else if (frame == 6) {
                              if (glm::length(playerTransform.scale - glm::vec3{1.0F}) > 0.0001F) {
                                  fail("legacy placeholder scale distorted the player model");
                              }
                              const auto bounds = runtime.modelBounds("models/player.glb");
                              if (!bounds ||
                                  std::abs(bounds->max.y - bounds->min.y - 1.8F) > 0.001F) {
                                  fail("player asset is not 1.8 metres tall");
                              }
                              const auto door = doorState();
                              if (progress.doorOpen || progress.interlockVerified || !door.body ||
                                  door.z != 0.0F || door.state != "locked") {
                                  fail("a save from before the puzzle did not start it afresh");
                              }
                              const auto current =
                                  gorden::captureSave(world, brain, progress, "scenes/room.json");
                              if (current.app.at("version") != 3) {
                                  fail("new saves must use app payload version 3");
                              }
                          }

                          if (frame == 59) {
                              movementStartX = playerTransform.position.x;
                          } else if (frame == 91) {
                              const auto facing =
                                  playerTransform.rotation * glm::vec3{0.0F, 0.0F, -1.0F};
                              if (playerTransform.position.x < movementStartX + 0.5F ||
                                  facing.x < 0.99F) {
                                  fail("model owner did not move and turn to the right");
                              }
                          }
                          world.forEach<roboslop::Camera, roboslop::Transform>([&](const auto&,
                                                                                   auto& camera) {
                              gorden::followPlayer(
                                  {.yaw = 0.6F, .pitch = -0.2F, .distance = 3.0F},
                                  playerTransform,
                                  camera,
                                  *world.registry().ctx().get<roboslop::JoltWorld*>()
                              );
                          });
                          roboslop::applyActiveCamera(world, c.viewId, c.viewportW, c.viewportH);
                          roboslop::uploadLights(world, uniforms);
                          auto draws = roboslop::collectMeshDraws(world, arena, c.viewId);
                          roboslop::sortDraws(draws);
                          roboslop::submitDraws(draws);
                          if (frame == 120) {
                              const auto screenshot = savePath.parent_path() / "player";
                              bgfx::requestScreenShot(BGFX_INVALID_HANDLE, screenshot.c_str());
                          }
                          if (++frame > 125 || failed) {
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
    if (!result) {
        std::println(stderr, "{}", result.error().context);
        return 1;
    }
    if (failed) {
        std::println(stderr, "gorden save smoke failed: {}", failure);
        return 1;
    }
    std::println("gorden save smoke passed ({} frames)", frame);
    return 0;
}
