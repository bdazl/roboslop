import gorden.interface;
import gorden.player;
import roboslop.ecs;
import roboslop.platform.input;
import roboslop.scene.runtime;
import roboslop.scene.transform;

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Escape leaves interactions before opening the pause menu", "[interface]") {
    using gorden::InterfaceMode;
    gorden::InterfaceState ui;
    ui.mode = InterfaceMode::Terminal;
    ui.chat();
    REQUIRE(ui.mode == InterfaceMode::Chat);
    REQUIRE_FALSE(ui.paused());
    ui.cancel();
    REQUIRE(ui.mode == InterfaceMode::Terminal);
    REQUIRE(ui.focusTerminal);
    ui.cancel();
    REQUIRE(ui.mode == InterfaceMode::Explore);
    ui.cancel();
    REQUIRE(ui.paused());
    ui.mode = InterfaceMode::Settings;
    ui.cancel();
    REQUIRE(ui.mode == InterfaceMode::Menu);
    REQUIRE(ui.paused());
    ui.cancel();
    REQUIRE_FALSE(ui.paused());
}

TEST_CASE("UI shortcuts respect typing, focus, range and held gamepad buttons", "[interface]") {
    using gorden::InterfaceMode;
    gorden::InterfaceState ui;
    roboslop::Input input;
    roboslop::InputSnapshot snapshot;
    snapshot.gamepad.connected = true;
    snapshot.gamepad.buttonA = true;
    input.beginFrame(snapshot);
    ui.update(input, false, false);
    REQUIRE(ui.mode == InterfaceMode::Explore);
    ui.update(input, false, true);
    REQUIRE(ui.mode == InterfaceMode::Explore); // holding A cannot reopen an interaction
    snapshot.gamepad.buttonA = false;
    input.beginFrame(snapshot);
    ui.update(input, false, true);
    snapshot.gamepad.buttonA = true;
    input.beginFrame(snapshot);
    ui.update(input, false, true);
    REQUIRE(ui.mode == InterfaceMode::Terminal);

    ui.cancel();
    snapshot.gamepad.buttonA = false;
    snapshot.keys[static_cast<int>(roboslop::Key::T)] = true;
    input.beginFrame(snapshot);
    ui.update(input, true, true);
    REQUIRE(ui.mode == InterfaceMode::Explore);
    snapshot.keys[static_cast<int>(roboslop::Key::T)] = false;
    input.beginFrame(snapshot);
    ui.update(input, false, true);
    snapshot.keys[static_cast<int>(roboslop::Key::T)] = true;
    snapshot.focused = false;
    input.beginFrame(snapshot);
    ui.update(input, false, true);
    REQUIRE(ui.mode == InterfaceMode::Explore);
}

TEST_CASE("Chat consumes movement and releases camera capture", "[interface]") {
    roboslop::Input input;
    roboslop::InputSnapshot snapshot;
    snapshot.keys[static_cast<int>(roboslop::Key::W)] = true;
    snapshot.mouseButtons[static_cast<int>(roboslop::MouseButton::Right)] = true;
    snapshot.gamepad.connected = true;
    snapshot.gamepad.leftStick = {1.0F, 0.0F};
    snapshot.gamepad.rightStick = {1.0F, 0.0F};
    input.beginFrame(snapshot);
    input.setCursorCaptured(true);
    gorden::PlayerControls controls{.uiKeyboard = true};
    const auto movement = gorden::readPlayerInput(input, controls);
    REQUIRE(movement.move.x == 0.0F);
    REQUIRE(movement.move.y == 0.0F);
    REQUIRE(movement.stickLook.x == 0.0F);
    REQUIRE_FALSE(input.cursorCaptured());
}

TEST_CASE("Chat opens away from the computer and held Escape only cancels once", "[interface]") {
    using gorden::InterfaceMode;
    gorden::InterfaceState ui;
    roboslop::Input input;
    roboslop::InputSnapshot snapshot;
    snapshot.keys[static_cast<int>(roboslop::Key::T)] = true;
    input.beginFrame(snapshot);
    ui.update(input, false, false);
    REQUIRE(ui.mode == InterfaceMode::Chat);
    REQUIRE(ui.focusChat);

    snapshot.keys[static_cast<int>(roboslop::Key::T)] = false;
    snapshot.keys[static_cast<int>(roboslop::Key::Escape)] = true;
    input.beginFrame(snapshot);
    ui.update(input, true, false);
    REQUIRE(ui.mode == InterfaceMode::Explore);
    input.beginFrame(snapshot);
    ui.update(input, false, false);
    REQUIRE(ui.mode == InterfaceMode::Explore);

    snapshot.keys[static_cast<int>(roboslop::Key::Escape)] = false;
    snapshot.gamepad.connected = true;
    snapshot.gamepad.buttonStart = true;
    input.beginFrame(snapshot);
    ui.update(input, false, false);
    REQUIRE(ui.paused());
    ui.chat();
    REQUIRE(ui.mode == InterfaceMode::Menu);
}

TEST_CASE("Computer access follows the live authored target", "[interface]") {
    roboslop::World world;
    REQUIRE_FALSE(gorden::computerInReach(world, {0.0F, 0.0F, 0.0F}));
    const auto computer = world.create();
    world.emplace<roboslop::SceneIdentity>(
        computer, roboslop::SceneIdentity{.id = "terminal-monitor", .name = "Monitor"}
    );
    auto& transform = world.emplace<roboslop::Transform>(
        computer, roboslop::Transform{.position = {0.0F, 0.8F, -4.9F}}
    );
    REQUIRE(gorden::computerInReach(world, {0.0F, 0.9F, -3.5F}));
    REQUIRE_FALSE(gorden::computerInReach(world, {0.0F, 0.9F, 4.0F}));
    transform.position = {5.0F, 0.8F, 0.0F};
    REQUIRE_FALSE(gorden::computerInReach(world, {0.0F, 0.9F, -3.5F}));
    world.destroy(computer);
    REQUIRE_FALSE(gorden::computerInReach(world, {5.0F, 0.9F, 0.0F}));
}
