module;

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

export module gorden.interface;

import roboslop.ecs;
import roboslop.platform.input;
import roboslop.scene.runtime;
import roboslop.scene.transform;

namespace gorden {

export enum class InterfaceMode { Explore, Chat, Terminal, Menu, Settings };

// One owner for gameplay input. Updated once per frame, before fixed steps.
export struct InterfaceState {
    InterfaceMode mode = InterfaceMode::Explore;
    InterfaceMode returnMode = InterfaceMode::Explore;
    bool developer = false;
    bool focusChat = false;
    bool focusTerminal = false;
    roboslop::GamepadSnapshot previousPad{};

    [[nodiscard]] auto paused() const -> bool {
        return mode == InterfaceMode::Menu || mode == InterfaceMode::Settings;
    }

    auto chat() -> void {
        if (paused()) {
            return;
        }
        returnMode = mode == InterfaceMode::Terminal ? mode : InterfaceMode::Explore;
        mode = InterfaceMode::Chat;
        focusChat = true;
    }

    auto cancel() -> void {
        switch (mode) {
        case InterfaceMode::Explore:
        case InterfaceMode::Settings:
            mode = InterfaceMode::Menu;
            break;
        case InterfaceMode::Chat:
            mode = returnMode;
            focusTerminal = mode == InterfaceMode::Terminal;
            break;
        case InterfaceMode::Terminal:
        case InterfaceMode::Menu:
            mode = InterfaceMode::Explore;
            break;
        }
    }

    auto update(roboslop::Input& input, bool typing, bool computerNearby) -> void {
        const auto pad = input.gamepad();
        const bool cancelPressed =
            input.keyPressed(roboslop::Key::Escape) || (pad.buttonB && !previousPad.buttonB);
        const bool chatPressed =
            input.keyPressed(roboslop::Key::T) || (pad.buttonX && !previousPad.buttonX);
        const bool usePressed =
            input.keyPressed(roboslop::Key::E) || (pad.buttonA && !previousPad.buttonA);
        const bool menuPressed = pad.buttonStart && !previousPad.buttonStart;
        previousPad = pad;
        if (!input.focused()) {
            return;
        }
        if (cancelPressed) {
            cancel();
        } else if (menuPressed && mode == InterfaceMode::Explore) {
            mode = InterfaceMode::Menu;
        } else if (!typing && mode == InterfaceMode::Explore) {
            if (chatPressed) {
                chat();
            } else if (usePressed && computerNearby) {
                mode = InterfaceMode::Terminal;
                focusTerminal = true;
            }
        }
    }
};

// First room's authored monitor is the interaction target. Use its live
// transform so loading a save does not leave a cached entity or position.
export [[nodiscard]] auto computerInReach(roboslop::World& world, glm::vec3 player) -> bool {
    bool reachable = false;
    world.forEach<roboslop::SceneIdentity, roboslop::Transform>([&](const auto& identity,
                                                                    const auto& transform) {
        if (identity.id == "terminal-monitor" &&
            glm::distance(player, transform.position) <= 2.0F) {
            reachable = true;
        }
    });
    return reachable;
}

} // namespace gorden
