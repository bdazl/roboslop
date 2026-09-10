import roboslop.scene.document;
import roboslop.render.lighting;
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <limits>

TEST_CASE("Saving and reloading preserves scenes and rejects invalid overwrites", "[scene]") {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir =
        std::filesystem::temp_directory_path() / ("roboslop-scene-" + std::to_string(unique));
    const auto file = dir / "room.json";
    roboslop::SceneDocument document;
    document.objects.push_back({.id = "crate", .name = "Låda"});
    const auto expected = roboslop::sceneToJson(document);
    REQUIRE(roboslop::saveScene(file, document));
    document.objects[0].transform.scale.x = 0;
    REQUIRE_FALSE(roboslop::saveScene(file, document));
    const auto reopened = roboslop::loadScene(file);
    REQUIRE(reopened);
    REQUIRE(roboslop::sceneToJson(*reopened) == expected);
    REQUIRE_FALSE(std::filesystem::exists(file.string() + ".tmp"));
    std::filesystem::remove(file);
    std::filesystem::remove(dir);
}

TEST_CASE("Scene document round trips without runtime handles", "[scene]") {
    roboslop::SceneDocument scene;
    scene.objects.push_back({.id = "box", .name = "Crate", .body = "dynamic"});
    scene.pointLight = roboslop::PointLight{
        .position = {1.0F, 2.0F, 3.0F},
        .color = {1.0F, 0.8F, 0.6F},
        .intensity = 4.0F,
        .range = 10.0F,
    };
    const auto json = roboslop::sceneToJson(scene);
    auto decoded = roboslop::sceneFromJson(json);
    REQUIRE(decoded);
    REQUIRE(roboslop::sceneToJson(*decoded) == json);
    REQUIRE_FALSE(json.dump().contains("RigidBody"));
    REQUIRE(json["pointLight"]["position"] == nlohmann::json::array({1.0F, 2.0F, 3.0F}));
}

TEST_CASE("Version one scenes may omit the optional point light", "[scene]") {
    const auto json = roboslop::sceneToJson({});
    REQUIRE_FALSE(json.contains("pointLight"));
    const auto decoded = roboslop::sceneFromJson(json);
    REQUIRE(decoded);
    REQUIRE_FALSE(decoded->pointLight.has_value());
}

TEST_CASE("Model objects round trip and primitives omit the model key", "[scene]") {
    roboslop::SceneDocument scene;
    scene.objects.push_back({.id = "box"});
    scene.objects.push_back(
        {.id = "crate", .geometry = "model", .model = "models/crate.glb", .body = "dynamic"}
    );
    const auto json = roboslop::sceneToJson(scene);
    REQUIRE_FALSE(json["objects"][0].contains("model"));
    REQUIRE(json["objects"][1]["model"] == "models/crate.glb");
    auto decoded = roboslop::sceneFromJson(json);
    REQUIRE(decoded);
    REQUIRE(decoded->objects[1].model == "models/crate.glb");
    REQUIRE(roboslop::sceneToJson(*decoded) == json);
}

TEST_CASE("Scene validation rejects invalid references and physics geometry", "[scene]") {
    roboslop::SceneDocument scene;
    scene.objects.push_back({.id = "box"});
    SECTION("duplicate identity") {
        scene.objects.push_back(scene.objects[0]);
    }
    SECTION("unknown material") {
        scene.objects[0].material = "missing";
    }
    SECTION("unknown primitive") {
        scene.objects[0].geometry = "mesh";
    }
    SECTION("unknown body") {
        scene.objects[0].body = "kinematic";
    }
    SECTION("negative scale") {
        scene.objects[0].transform.scale.x = -1;
    }
    SECTION("nonfinite position") {
        scene.objects[0].transform.position.x = std::numeric_limits<float>::infinity();
    }
    SECTION("invalid rotation") {
        scene.objects[0].transform.rotation.w = 0;
    }
    SECTION("zero light direction") {
        scene.light.direction = {0, 0, 0};
    }
    SECTION("invalid point light range") {
        scene.pointLight.emplace();
        scene.pointLight->range = 0;
    }
    SECTION("nonuniform collider sphere") {
        scene.objects[0].geometry = "sphere";
        scene.objects[0].body = "dynamic";
        scene.objects[0].transform.scale.x = 2;
    }
    SECTION("model without a path") {
        scene.objects[0].geometry = "model";
    }
    SECTION("absolute model path") {
        scene.objects[0].geometry = "model";
        scene.objects[0].model = "/etc/crate.glb";
    }
    SECTION("model path escaping the asset root") {
        scene.objects[0].geometry = "model";
        scene.objects[0].model = "models/../../crate.glb";
    }
    SECTION("primitive with a model path") {
        scene.objects[0].model = "models/crate.glb";
    }
    REQUIRE_FALSE(roboslop::validateScene(scene));
}

TEST_CASE("Scene input errors are values and never partial documents", "[scene]") {
    auto json = roboslop::sceneToJson({});
    SECTION("future version") {
        json["version"] = 999;
    }
    SECTION("missing field") {
        json.erase("camera");
    }
    SECTION("wrong vector type") {
        json["light"]["direction"] = "down";
    }
    SECTION("wrong scalar type") {
        json["light"]["intensity"] = "bright";
    }
    SECTION("malformed point light") {
        json["pointLight"] = {{"position", "overhead"}};
    }
    REQUIRE_FALSE(roboslop::sceneFromJson(json));
}
