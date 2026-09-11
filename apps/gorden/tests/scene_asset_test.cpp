import gorden.robot_visual;
import roboslop.assets.mesh;
import roboslop.scene.document;
import roboslop.scene.transform;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>

namespace {

auto assets() -> std::filesystem::path {
    return std::filesystem::path{GORDEN_TEST_ASSETS};
}

auto findObject(const roboslop::SceneDocument& scene, std::string_view id)
    -> const roboslop::SceneObject* {
    const auto found = std::ranges::find(scene.objects, id, &roboslop::SceneObject::id);
    return found == scene.objects.end() ? nullptr : &*found;
}

auto loadBounds(const roboslop::SceneObject& object) -> roboslop::Aabb {
    const auto model = roboslop::loadModelFile(assets() / object.model);
    REQUIRE(model);
    return roboslop::modelBounds(*model);
}

auto worldMinY(const roboslop::SceneObject& object, const roboslop::Aabb& bounds) -> float {
    return object.transform.position.y + (bounds.min.y * object.transform.scale.y);
}

auto worldMaxY(const roboslop::SceneObject& object, const roboslop::Aabb& bounds) -> float {
    return object.transform.position.y + (bounds.max.y * object.transform.scale.y);
}

} // namespace

TEST_CASE("The authored room grounds props on explicit support surfaces", "[gorden][scene]") {
    const auto loaded = roboslop::loadScene(assets() / "scenes/room.json");
    REQUIRE(loaded);
    const auto& scene = *loaded;

    const auto* floor = findObject(scene, "floor");
    REQUIRE(floor != nullptr);
    const float floorSurface = floor->transform.position.y + (floor->transform.scale.y * 0.5F);
    REQUIRE(floorSurface == Catch::Approx(0.0F));

    for (const std::string_view id :
         {"terminal-desk", "terminal-tower", "terminal-chair", "power-unit"}) {
        const auto* object = findObject(scene, id);
        REQUIRE(object != nullptr);
        const auto bounds = loadBounds(*object);
        CAPTURE(id, bounds.min.y, object->transform.position.y);
        REQUIRE(worldMinY(*object, bounds) == Catch::Approx(floorSurface).margin(0.001F));
    }

    const auto* desk = findObject(scene, "terminal-desk");
    REQUIRE(desk != nullptr);
    const auto deskBounds = loadBounds(*desk);
    const float deskSurface = worldMaxY(*desk, deskBounds);
    REQUIRE(deskSurface == Catch::Approx(0.8F).margin(0.001F));

    for (const std::string_view id : {"terminal-monitor", "terminal-keyboard"}) {
        const auto* object = findObject(scene, id);
        REQUIRE(object != nullptr);
        const auto bounds = loadBounds(*object);
        CAPTURE(id, bounds.min.y, object->transform.position.y);
        REQUIRE(worldMinY(*object, bounds) == Catch::Approx(deskSurface).margin(0.001F));
    }

    const auto* keyboard = findObject(scene, "terminal-keyboard");
    REQUIRE(keyboard != nullptr);
    REQUIRE(
        keyboard->transform.position.x >=
        desk->transform.position.x + (deskBounds.min.x * desk->transform.scale.x)
    );
    REQUIRE(
        keyboard->transform.position.x <=
        desk->transform.position.x + (deskBounds.max.x * desk->transform.scale.x)
    );
    REQUIRE(
        keyboard->transform.position.z >=
        desk->transform.position.z + (deskBounds.min.z * desk->transform.scale.z)
    );
    REQUIRE(
        keyboard->transform.position.z <=
        desk->transform.position.z + (deskBounds.max.z * desk->transform.scale.z)
    );

    const auto* ceiling = findObject(scene, "ceiling");
    const auto* lightFixture = findObject(scene, "ceiling-light-fixture");
    const auto* lightGlobe = findObject(scene, "ceiling-light-globe");
    REQUIRE(ceiling != nullptr);
    REQUIRE(lightFixture != nullptr);
    REQUIRE(lightGlobe != nullptr);

    const float ceilingUnderside =
        ceiling->transform.position.y - (ceiling->transform.scale.y * 0.5F);
    const float fixtureTop =
        lightFixture->transform.position.y + (lightFixture->transform.scale.y * 0.5F);
    const float fixtureBottom =
        lightFixture->transform.position.y - (lightFixture->transform.scale.y * 0.5F);
    const float globeTop =
        lightGlobe->transform.position.y + (lightGlobe->transform.scale.y * 0.5F);
    REQUIRE(fixtureTop == Catch::Approx(ceilingUnderside));
    REQUIRE(globeTop == Catch::Approx(fixtureBottom));
    REQUIRE(scene.pointLight.has_value());
    REQUIRE(scene.pointLight->position.x == Catch::Approx(lightGlobe->transform.position.x));
    REQUIRE(scene.pointLight->position.y == Catch::Approx(lightGlobe->transform.position.y));
    REQUIRE(scene.pointLight->position.z == Catch::Approx(lightGlobe->transform.position.z));
}

TEST_CASE("Conduit bay C hides behind the power unit", "[gorden][scene]") {
    const auto loaded = roboslop::loadScene(assets() / "scenes/room.json");
    REQUIRE(loaded);
    const auto& scene = *loaded;
    const auto* wall = findObject(scene, "wall-back");
    const auto* unit = findObject(scene, "power-unit");
    const auto* bay = findObject(scene, "conduit-bay-c");
    REQUIRE(wall != nullptr);
    REQUIRE(unit != nullptr);
    REQUIRE(bay != nullptr);

    const float wallFace = wall->transform.position.z + (wall->transform.scale.z * 0.5F);
    const auto unitBounds = loadBounds(*unit);
    const float unitBack =
        unit->transform.position.z + (unitBounds.min.z * unit->transform.scale.z);
    const float bayBack = bay->transform.position.z - (bay->transform.scale.z * 0.5F);
    const float bayFront = bay->transform.position.z + (bay->transform.scale.z * 0.5F);

    // Mounted on the wall, clear of the cabinet, and in a gap the
    // player's 0.35 m capsule cannot enter (the robot has no body).
    REQUIRE(bayBack == Catch::Approx(wallFace).margin(0.001F));
    REQUIRE(bayFront < unitBack);
    REQUIRE(unitBack - wallFace < 2.0F * 0.35F);
    REQUIRE(
        std::abs(bay->transform.position.x - unit->transform.position.x) <
        unitBounds.max.x * unit->transform.scale.x
    );
    REQUIRE(
        bay->transform.position.y + (bay->transform.scale.y * 0.5F) <
        unit->transform.position.y + (unitBounds.max.y * unit->transform.scale.y)
    );
}

TEST_CASE("The robot visual shares its actor's ground contact", "[gorden][model]") {
    const auto model = roboslop::loadModelFile(assets() / "models/gorden.glb");
    REQUIRE(model);
    const auto bounds = roboslop::modelBounds(*model);
    const auto visual = gorden::robotVisualTransform();

    REQUIRE(
        visual.position.y + (bounds.min.y * visual.scale.y) == Catch::Approx(0.0F).margin(0.001F)
    );
}
