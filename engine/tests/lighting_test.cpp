import roboslop.render.lighting;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/vec3.hpp>

TEST_CASE("packDirectionalLightUniform normalises the direction", "[render][lighting]") {
    const roboslop::DirectionalLight light{
        .direction = {0.0F, -2.0F, 0.0F},
        .color = {1.0F, 0.5F, 0.25F},
        .intensity = 0.75F,
    };
    const auto packed = roboslop::packDirectionalLightUniform(light);

    REQUIRE(packed[0].x == Catch::Approx(0.0F));
    REQUIRE(packed[0].y == Catch::Approx(-1.0F));
    REQUIRE(packed[0].z == Catch::Approx(0.0F));
    REQUIRE(packed[0].w == Catch::Approx(0.75F));

    REQUIRE(packed[1].x == Catch::Approx(1.0F));
    REQUIRE(packed[1].y == Catch::Approx(0.5F));
    REQUIRE(packed[1].z == Catch::Approx(0.25F));
    REQUIRE(packed[1].w == Catch::Approx(0.0F));
}

TEST_CASE("packDirectionalLightUniform handles already-unit-length dirs", "[render][lighting]") {
    const roboslop::DirectionalLight light{
        .direction = {0.6F, 0.0F, -0.8F}, // already length 1
        .color = {1.0F, 1.0F, 1.0F},
        .intensity = 1.0F,
    };
    const auto packed = roboslop::packDirectionalLightUniform(light);

    REQUIRE(packed[0].x == Catch::Approx(0.6F));
    REQUIRE(packed[0].z == Catch::Approx(-0.8F));
}

TEST_CASE(
    "packPointLightUniform carries world position range color and intensity", "[render][lighting]"
) {
    const roboslop::PointLight light{
        .position = {1.0F, 2.0F, 3.0F},
        .color = {1.0F, 0.8F, 0.6F},
        .intensity = 4.0F,
        .range = 10.0F,
    };
    const auto packed = roboslop::packPointLightUniform(light);

    REQUIRE(packed[0].x == Catch::Approx(1.0F));
    REQUIRE(packed[0].y == Catch::Approx(2.0F));
    REQUIRE(packed[0].z == Catch::Approx(3.0F));
    REQUIRE(packed[0].w == Catch::Approx(10.0F));
    REQUIRE(packed[1].x == Catch::Approx(1.0F));
    REQUIRE(packed[1].y == Catch::Approx(0.8F));
    REQUIRE(packed[1].z == Catch::Approx(0.6F));
    REQUIRE(packed[1].w == Catch::Approx(4.0F));
}
