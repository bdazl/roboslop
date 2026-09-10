import editor.model;
import roboslop.app;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.platform.input;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.free_fly_camera;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.lighting;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.transform;
import roboslop.sched;
import roboslop.ui;

#include <bgfx/bgfx.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>

namespace {
struct EditorState {
    editor::History history;
    nlohmann::json saved;
    std::array<char, 1024> path{};
    std::string currentPath;
    std::string pendingPath;
    bool confirmOverwrite = false;
    std::string selected;
    std::string status;
    std::string pending; // New/Open/Exit, only executed after unsaved-change handling.
    bool playing = false;
    bool rebuild = true;
    bool uiWantsMouse = false;
    bool allowClose = false;
    int operation = 0;
    int dragAxis = -1;
    ImVec2 dragStart{};
    ImVec2 dragDirection{};
    float dragPixels = 1;
    roboslop::Transform dragTransform;
    std::optional<roboslop::SceneDocument> transaction;
    editor::ModelBounds modelBounds; // refreshed after every runtime rebuild
    roboslop::Entity camera = roboslop::NullEntity;
    roboslop::LightUniforms light;

    [[nodiscard]] auto dirty() const -> bool {
        return saved != roboslop::sceneToJson(history.document);
    }

    auto finishEdit() -> void {
        if (transaction) {
            history.checkpoint(*transaction);
            transaction.reset();
        }
    }

    auto selectedObject() -> roboslop::SceneObject* {
        auto& objects = history.document.objects;
        auto found = std::ranges::find(objects, selected, &roboslop::SceneObject::id);
        return found == objects.end() ? nullptr : &*found;
    }
};

auto save(EditorState& state, bool currentFile = false, bool overwriteConfirmed = false) -> bool {
    state.finishEdit();
    const std::string target = currentFile ? state.currentPath : state.path.data();
    if (target.empty()) {
        state.status = "Cancel, enter a file path, and Save first.";
        return false;
    }
    std::error_code ec;
    if (!currentFile && !overwriteConfirmed && target != state.currentPath &&
        std::filesystem::exists(target, ec)) {
        state.confirmOverwrite = true;
        return false;
    }
    auto result = roboslop::saveScene(target, state.history.document);
    if (!result) {
        state.status = std::string(result.error().message) + ": " + result.error().context;
        return false;
    }
    state.saved = roboslop::sceneToJson(state.history.document);
    state.currentPath = target;
    state.status = "Scene saved.";
    return true;
}

auto resetCamera(roboslop::World& world, EditorState& state) -> void {
    world.get<roboslop::Transform>(state.camera) = state.history.document.camera;
    const auto angles = glm::eulerAngles(state.history.document.camera.rotation);
    auto& fly = world.get<roboslop::FreeFlyCamera>(state.camera);
    fly.yawRadians = angles.y;
    fly.pitchRadians = angles.x;
}

auto executePending(roboslop::World& world, EditorState& state) -> void {
    if (state.pending == "Exit") {
        state.allowClose = true;
        roboslop::requestAppClose(world);
    } else if (state.pending == "New") {
        state.history.reset({});
        state.saved = roboslop::sceneToJson(state.history.document);
        state.path.fill('\0');
        state.currentPath.clear();
        state.selected.clear();
        state.rebuild = true;
        resetCamera(world, state);
    } else if (state.pending == "Open") {
        auto loaded = roboslop::loadScene(state.pendingPath);
        if (loaded) {
            state.history.reset(std::move(*loaded));
            state.saved = roboslop::sceneToJson(state.history.document);
            state.currentPath = state.pendingPath;
            state.selected.clear();
            state.rebuild = true;
            resetCamera(world, state);
            state.status = "Scene opened.";
        } else {
            state.status = std::string(loaded.error().message) + ": " + loaded.error().context;
        }
    }
    state.pending.clear();
}

auto drawToolbar(roboslop::World& world, EditorState& state) -> void {
    ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({570, 0}, ImGuiCond_Always);
    ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    const char* mode = "EDIT";
    if (state.playing) {
        mode = "PLAY — Stop restores the authored scene";
    } else if (state.dirty()) {
        mode = "EDIT — unsaved changes";
    }
    ImGui::TextUnformatted(mode);
    if (ImGui::Button(state.playing ? "Stop" : "Play")) {
        state.finishEdit();
        state.playing = !state.playing;
        state.rebuild = true;
    }
    ImGui::BeginDisabled(state.playing);
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        state.pending = "New";
    }
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        state.pending = "Open";
        state.pendingPath = state.path.data();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        (void)save(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo")) {
        state.finishEdit();
        state.rebuild |= state.history.undo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo")) {
        state.finishEdit();
        state.rebuild |= state.history.redo();
    }
    ImGui::InputText("File", state.path.data(), state.path.size());
    ImGui::TextUnformatted("Open/Save use this path. Relative paths start in build/debug.");
    if (ImGui::Button("Use current camera as scene start")) {
        const auto before = state.history.document;
        state.history.document.camera = world.get<roboslop::Transform>(state.camera);
        state.history.checkpoint(before);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("RMB + WASD: fly | Space/Ctrl: up/down | Shift: faster");
    ImGui::TextWrapped("Click an object to select. Drag a colored axis handle to edit.");
    ImGui::TextWrapped("%s", state.status.c_str());
    ImGui::End();

    if (state.confirmOverwrite) {
        ImGui::OpenPopup("Replace existing scene?");
    }
    if (ImGui::BeginPopupModal(
            "Replace existing scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize
        )) {
        ImGui::TextWrapped("Replace the file at %s?", state.path.data());
        if (ImGui::Button("Replace")) {
            (void)save(state, false, true);
            state.confirmOverwrite = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.confirmOverwrite = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!state.pending.empty()) {
        state.finishEdit();
        if (!state.dirty()) {
            executePending(world, state);
        } else {
            ImGui::OpenPopup("Unsaved changes");
        }
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save your scene before continuing?");
        ImGui::TextWrapped("Save changes to the current document: %s", state.currentPath.c_str());
        if (ImGui::Button("Save and continue") && save(state, true)) {
            executePending(world, state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes")) {
            executePending(world, state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.pending.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

auto drawInspector(EditorState& state) -> void {
    ImGui::SetNextWindowPos({8, 260}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({330, 470}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Objects and properties");
    ImGui::BeginDisabled(state.playing);
    const auto before = state.history.document;
    for (const auto* type : {"cube", "sphere", "plane"}) {
        if (ImGui::Button(type)) {
            auto object = roboslop::SceneObject{
                .id = editor::nextId(state.history.document),
                .name = type,
                .geometry = type,
                .material = state.history.document.materials.begin()->first
            };
            state.selected = object.id;
            state.history.document.objects.push_back(std::move(object));
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    const auto models = editor::listModels("assets");
    if (!models.empty()) {
        ImGui::TextUnformatted("Models:");
    }
    for (const auto& model : models) {
        ImGui::SameLine();
        const auto stem = std::filesystem::path{model}.stem().string();
        if (ImGui::Button(stem.c_str())) {
            auto object = roboslop::SceneObject{
                .id = editor::nextId(state.history.document),
                .name = stem,
                .geometry = "model",
                .model = model,
                .material = state.history.document.materials.begin()->first
            };
            state.selected = object.id;
            state.history.document.objects.push_back(std::move(object));
        }
    }
    if (!models.empty()) {
        ImGui::NewLine();
    }
    ImGui::BeginChild("Object list", {0, 130}, ImGuiChildFlags_Border);
    for (const auto& object : state.history.document.objects) {
        ImGui::PushID(object.id.c_str());
        if (ImGui::Selectable(object.name.c_str(), state.selected == object.id)) {
            state.finishEdit();
            state.selected = object.id;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (auto* object = state.selectedObject()) {
        if (ImGui::Button("Duplicate")) {
            auto copy = *object;
            copy.id = editor::nextId(state.history.document);
            copy.name += " copy";
            copy.transform.position.x += 1;
            state.selected = copy.id;
            state.history.document.objects.push_back(std::move(copy));
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            std::erase_if(state.history.document.objects, [&](const auto& o) {
                return o.id == state.selected;
            });
            state.selected.clear();
        }
    }
    if (auto* object = state.selectedObject()) {
        ImGui::Text("ID: %s", object->id.c_str());
        std::array<char, 256> name{};
        std::snprintf(name.data(), name.size(), "%s", object->name.c_str());
        if (ImGui::InputText("Name", name.data(), name.size())) {
            object->name = name.data();
        }
        ImGui::DragFloat3("Position", glm::value_ptr(object->transform.position), 0.05F);
        auto degrees = glm::degrees(glm::eulerAngles(object->transform.rotation));
        if (ImGui::DragFloat3("Rotation", glm::value_ptr(degrees), 0.5F)) {
            object->transform.rotation = glm::normalize(glm::quat(glm::radians(degrees)));
        }
        if (object->geometry == "sphere" && object->body != "none") {
            if (ImGui::DragFloat(
                    "Scale",
                    &object->transform.scale.x,
                    0.02F,
                    0.01F,
                    1000,
                    "%.2f",
                    ImGuiSliderFlags_AlwaysClamp
                )) {
                object->transform.scale = glm::vec3(object->transform.scale.x);
            }
        } else {
            ImGui::DragFloat3(
                "Scale",
                glm::value_ptr(object->transform.scale),
                0.02F,
                0.01F,
                1000,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp
            );
        }
        if (object->geometry == "model") {
            ImGui::Text("Model: %s", object->model.c_str());
            ImGui::TextWrapped("Materials and textures come from the model file.");
        } else {
            if (ImGui::BeginCombo("Material", object->material.c_str())) {
                for (const auto& [id, color] : state.history.document.materials) {
                    (void)color;
                    if (ImGui::Selectable(id.c_str(), object->material == id)) {
                        object->material = id;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::ColorEdit3(
                "Shared color",
                glm::value_ptr(state.history.document.materials.at(object->material))
            );
            ImGui::TextWrapped("Color changes all objects using this material.");
            if (ImGui::Button("Make material unique")) {
                std::string id = object->id + "-material";
                while (state.history.document.materials.contains(id)) {
                    id += "-copy";
                }
                state.history.document.materials[id] =
                    state.history.document.materials.at(object->material);
                object->material = id;
            }
        }
        if (object->geometry != "plane" && ImGui::BeginCombo("Physics", object->body.c_str())) {
            for (const auto* mode : {"none", "static", "dynamic"}) {
                if (ImGui::Selectable(mode, object->body == mode)) {
                    object->body = mode;
                    if (object->geometry == "sphere") {
                        object->transform.scale = glm::vec3(object->transform.scale.x);
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::RadioButton("Move", &state.operation, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Rotate", &state.operation, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Scale", &state.operation, 2);
    }
    if (ImGui::CollapsingHeader("Directional light")) {
        ImGui::DragFloat3(
            "Direction", glm::value_ptr(state.history.document.light.direction), 0.02F
        );
        ImGui::ColorEdit3("Light color", glm::value_ptr(state.history.document.light.color));
        ImGui::DragFloat(
            "Intensity",
            &state.history.document.light.intensity,
            0.02F,
            0,
            100,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        );
    }
    if (ImGui::CollapsingHeader("Point light")) {
        bool enabled = state.history.document.pointLight.has_value();
        if (ImGui::Checkbox("Enabled", &enabled)) {
            if (enabled) {
                state.history.document.pointLight.emplace();
            } else {
                state.history.document.pointLight.reset();
            }
        }
        if (state.history.document.pointLight) {
            auto& point = *state.history.document.pointLight;
            ImGui::DragFloat3("Position", glm::value_ptr(point.position), 0.02F);
            ImGui::ColorEdit3("Point color", glm::value_ptr(point.color));
            ImGui::DragFloat(
                "Point intensity",
                &point.intensity,
                0.02F,
                0,
                100,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp
            );
            ImGui::DragFloat(
                "Point range",
                &point.range,
                0.02F,
                0.01F,
                1000,
                "%.2f",
                ImGuiSliderFlags_AlwaysClamp
            );
        }
    }
    if (roboslop::sceneToJson(before) != roboslop::sceneToJson(state.history.document)) {
        if (auto valid = roboslop::validateScene(state.history.document); !valid) {
            state.status = valid.error().context;
            state.history.document = before;
        } else {
            if (!state.transaction) {
                state.transaction = before;
            }
            state.rebuild = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::End();
}

auto project(glm::vec3 point, const glm::mat4& vp) -> std::optional<ImVec2> {
    const auto clip = vp * glm::vec4(point, 1);
    if (clip.w < 0.01F) {
        return {};
    }
    const auto size = ImGui::GetIO().DisplaySize;
    return ImVec2{
        ((clip.x / clip.w * 0.5F) + 0.5F) * size.x, (0.5F - (clip.y / clip.w * 0.5F)) * size.y
    };
}

auto drawGizmo(EditorState& state, const glm::mat4& vp) -> bool {
    auto* object = state.selectedObject();
    if ((object == nullptr) || state.playing) {
        return false;
    }
    const auto origin = project(object->transform.position, vp);
    if (!origin) {
        return false;
    }
    const auto& io = ImGui::GetIO();
    const bool overPanel =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || !state.pending.empty();
    auto* draw = ImGui::GetBackgroundDrawList();
    // A wire box around the object's bounds makes the current selection
    // visible even when a large object's origin is hidden behind the
    // property panel.
    const auto model = roboslop::toMatrix(object->transform);
    const auto bounds = editor::objectBounds(*object, state.modelBounds)
                            .value_or({.min = glm::vec3{-0.5F}, .max = glm::vec3{0.5F}});
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local{
            (corner & 1) != 0 ? bounds.max.x : bounds.min.x,
            (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
            (corner & 4) != 0 ? bounds.max.z : bounds.min.z
        };
        for (int axis = 0; axis < 3; ++axis) {
            if ((corner & (1 << axis)) != 0) {
                continue;
            }
            auto other = local;
            other[axis] = bounds.max[axis];
            auto a = project(glm::vec3(model * glm::vec4(local, 1)), vp);
            auto b = project(glm::vec3(model * glm::vec4(other, 1)), vp);
            if (a && b) {
                draw->AddLine(*a, *b, IM_COL32(255, 210, 70, 220), 1.5F);
            }
        }
    }
    constexpr std::array<ImU32, 3> Colors{
        IM_COL32(255, 85, 85, 255), IM_COL32(85, 255, 120, 255), IM_COL32(80, 160, 255, 255)
    };
    bool hovered = false;
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 direction{0};
        direction[axis] = 1;
        if (state.operation == 2) {
            direction = object->transform.rotation * direction;
        }
        const auto endpoint = project(object->transform.position + direction, vp);
        if (!endpoint) {
            continue;
        }
        const ImVec2 delta{endpoint->x - origin->x, endpoint->y - origin->y};
        const float length = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
        if (length < 2) {
            continue;
        }
        const ImVec2 unit{delta.x / length, delta.y / length};
        const ImVec2 handle{origin->x + (unit.x * 85), origin->y + (unit.y * 85)};
        draw->AddLine(*origin, handle, Colors[static_cast<std::size_t>(axis)], 3);
        draw->AddCircleFilled(handle, 7, Colors[static_cast<std::size_t>(axis)]);
        const auto* label = std::array{"X", "Y", "Z"}[static_cast<std::size_t>(axis)];
        draw->AddText({handle.x + 9, handle.y}, Colors[static_cast<std::size_t>(axis)], label);
        const float dx = io.MousePos.x - handle.x;
        const float dy = io.MousePos.y - handle.y;
        const bool hit = !overPanel && (dx * dx) + (dy * dy) < 225;
        hovered |= hit;
        if (hit && state.dragAxis < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            state.finishEdit();
            state.transaction = state.history.document;
            state.dragAxis = axis;
            state.dragStart = io.MousePos;
            state.dragDirection = unit;
            state.dragPixels = length;
            state.dragTransform = object->transform;
        }
    }
    if (state.dragAxis >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.dragAxis = -1;
            state.finishEdit();
        } else {
            const float delta = ((io.MousePos.x - state.dragStart.x) * state.dragDirection.x) +
                                ((io.MousePos.y - state.dragStart.y) * state.dragDirection.y);
            auto transform = state.dragTransform;
            if (state.operation == 0) {
                transform.position[state.dragAxis] += delta / state.dragPixels;
            } else if (state.operation == 1) {
                glm::vec3 axis{0};
                axis[state.dragAxis] = 1;
                transform.rotation =
                    glm::normalize(glm::angleAxis(delta * 0.01F, axis) * transform.rotation);
            } else {
                transform.scale[state.dragAxis] = std::clamp(
                    transform.scale[state.dragAxis] * std::exp(delta * 0.01F), 0.01F, 1000.0F
                );
                if (object->geometry == "sphere" && object->body != "none") {
                    transform.scale = glm::vec3(transform.scale[state.dragAxis]);
                }
            }
            object->transform = transform;
            state.rebuild = true;
        }
    }
    return hovered || state.dragAxis >= 0;
}

auto drawEditor(roboslop::PassCtx& pass) -> void {
    auto& world = *pass.world;
    auto& state = world.registry().ctx().get<EditorState>();
    auto* ui = roboslop::devUi(world);
    ui->beginFrame();
    drawToolbar(world, state);
    drawInspector(state);
    const auto& camera = world.get<roboslop::Transform>(state.camera);
    const auto size = ImGui::GetIO().DisplaySize;
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;
    const auto projection = roboslop::projectionMatrix(
        world.get<roboslop::Camera>(state.camera), size.y > 0 ? size.x / size.y : 1, homogeneous
    );
    const auto vp = projection * roboslop::viewMatrix(camera);
    const bool gizmo = drawGizmo(state, vp);
    if (!state.playing && !gizmo && state.pending.empty() && !ui->wantCaptureMouse() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && size.x > 0 && size.y > 0) {
        const auto mouse = ImGui::GetIO().MousePos;
        const float x = (mouse.x / size.x * 2) - 1;
        const float y = 1 - (mouse.y / size.y * 2);
        const auto inverse = glm::inverse(vp);
        const auto nearPoint = inverse * glm::vec4(x, y, homogeneous ? -1 : 0, 1);
        const auto farPoint = inverse * glm::vec4(x, y, 1, 1);
        const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
        const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - origin);
        state.selected =
            editor::pickObject(state.history.document, origin, direction, state.modelBounds);
    }
    if (!ImGui::IsAnyItemActive() && state.dragAxis < 0) {
        state.finishEdit();
    }
    state.uiWantsMouse = ui->wantCaptureMouse() || gizmo || !state.pending.empty();
    ui->endFrame(pass.viewId);
}

auto graphs(
    roboslop::SystemGraph& fixed, roboslop::RenderGraph& render, roboslop::FrameArena& arena
) -> void {
    fixed.add(
        {.name = "editorCamera",
         .reads = {"input"},
         .writes = {"transforms"},
         .run = [](roboslop::SystemCtx& c) {
             const auto& state = c.world->registry().ctx().get<EditorState>();
             roboslop::updateFreeFlyCameras(*c.world, *c.input, c.dt, !state.uiWantsMouse);
         }}
    );
    fixed.add(
        {.name = "previewPhysics",
         .reads = {},
         .writes = {"transforms", "physics"},
         .run = [](roboslop::SystemCtx& c) {
             const auto& state = c.world->registry().ctx().get<EditorState>();
             if (state.playing && !state.rebuild) {
                 roboslop::physicsSpawn(c);
                 roboslop::physicsStep(c);
                 roboslop::syncPhysicsToTransform(c);
             }
         }}
    );
    render.add(
        {.name = "scene",
         .reads = {"transforms"},
         .writes = {"framebuffer"},
         .record = [&arena](roboslop::PassCtx& c) {
             auto& state = c.world->registry().ctx().get<EditorState>();
             if (state.rebuild) {
                 auto& runtime = c.world->registry().ctx().get<roboslop::SceneRuntime>();
                 auto result =
                     runtime.replace(*c.world, *c.assets, state.history.document, state.playing);
                 if (!result) {
                     state.status = result.error().context;
                 }
                 state.modelBounds.clear();
                 for (const auto& object : state.history.document.objects) {
                     if (const auto bounds = runtime.modelBounds(object.model)) {
                         state.modelBounds.emplace(object.model, *bounds);
                     }
                 }
                 state.rebuild = false;
             }
             roboslop::applyActiveCamera(*c.world, c.viewId, c.viewportW, c.viewportH);
             roboslop::uploadLights(*c.world, state.light);
             auto draws = roboslop::collectMeshDraws(*c.world, arena, c.viewId);
             roboslop::sortDraws(draws);
             roboslop::submitDraws(draws);
         }}
    );
    render.add(
        {.name = "editorUi",
         .reads = {"framebuffer"},
         .writes = {"framebuffer"},
         .record = drawEditor}
    );
}
} // namespace

auto main(int argc, char** argv) -> int {
    const std::filesystem::path path = argc == 2 ? argv[1] : "assets/scenes/room.json";
    if (argc > 2) {
        std::println(stderr, "Usage: editor [scene.json]");
        return 1;
    }
    auto document = roboslop::loadScene(path);
    if (!document) {
        std::println(
            stderr, "Cannot open scene: {} ({})", document.error().message, document.error().context
        );
        return 1;
    }
    auto app = roboslop::App::make(
        {.window = {.title = "roboslop-editor", .width = 1600, .height = 1000},
         .assetRoot = "assets",
         .enableDevUi = true,
         .closeOnEscape = false,
         .onCloseRequested =
             [](roboslop::World& world) {
                 auto& state = world.registry().ctx().get<EditorState>();
                 state.finishEdit();
                 if (state.allowClose || !state.dirty()) {
                     return true;
                 }
                 state.pending = "Exit";
                 return false;
             },
         .onSetup = [scene = *document, path](roboslop::World& world, roboslop::AssetCache& assets)
             -> roboslop::Result<void> {
             auto& state = world.registry().ctx().emplace<EditorState>();
             state.history.reset(scene);
             state.saved = roboslop::sceneToJson(scene);
             state.currentPath = path.string();
             std::snprintf(state.path.data(), state.path.size(), "%s", path.c_str());
             state.camera = world.create();
             world.emplace<roboslop::Transform>(state.camera, scene.camera);
             world.emplace<roboslop::Camera>(
                 state.camera, roboslop::Camera{.projection = roboslop::Perspective{}}
             );
             world.emplace<roboslop::ActiveCamera>(state.camera);
             world.emplace<roboslop::FreeFlyCamera>(state.camera);
             resetCamera(world, state);
             state.light = {
                 .dir = assets.uniform("u_lightDir", bgfx::UniformType::Vec4),
                 .color = assets.uniform("u_lightColor", bgfx::UniformType::Vec4),
                 .pointPosition = assets.uniform("u_pointLightPosition", bgfx::UniformType::Vec4),
                 .pointColor = assets.uniform("u_pointLightColor", bgfx::UniformType::Vec4)
             };
             auto& runtime = world.registry().ctx().emplace<roboslop::SceneRuntime>();
             auto loaded = runtime.replace(world, assets, scene, false);
             state.rebuild = false;
             return loaded;
         },
         .onBuildGraphs = graphs}
    );
    if (!app) {
        std::println(stderr, "Editor init: {} ({})", app.error().message, app.error().context);
        return 1;
    }
    const auto result = app->run();
    if (!result) {
        std::println(stderr, "Editor: {} ({})", result.error().message, result.error().context);
        return 1;
    }
    return 0;
}
