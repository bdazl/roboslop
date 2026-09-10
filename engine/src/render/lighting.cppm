module;

#include <bgfx/bgfx.h>

// entt's sparse-set iterator's operator!= is non-member and invisible
// across the module boundary for single-component range-for views;
// the include keeps the range-for views in uploadLights valid
// (see docs/decisions.md, 2026-05-17 ECS-facade entry).
#include <entt/entt.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

export module roboslop.render.lighting;

import roboslop.ecs;

namespace roboslop {

// Directional light: an infinitely distant source. `direction` points
// FROM the light TO surfaces (the convention the shader expects). The
// vector does not need to be normalised — packDirectionalLightUniform
// normalises it.
export struct DirectionalLight {
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
};

// Point light with a smooth finite range. `position` is in world space.
export struct PointLight {
    glm::vec3 position{0.0F, 3.0F, 0.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 10.0F;
};

// Bundle of bgfx uniform handles for the forward-lighting pass. Game
// code stashes one of these on the world's ctx storage at setup so the
// pass record callback can find it each frame without capturing it in
// the lambda.
export struct LightUniforms {
    bgfx::UniformHandle dir{bgfx::kInvalidHandle};
    bgfx::UniformHandle color{bgfx::kInvalidHandle};
    bgfx::UniformHandle pointPosition{bgfx::kInvalidHandle};
    bgfx::UniformHandle pointColor{bgfx::kInvalidHandle};
};

// Pack a DirectionalLight into the two vec4s the shader binds:
//   u_lightDir   = vec4(normalize(direction), intensity)
//   u_lightColor = vec4(color, 0)
// Returned as a pair so the engine can `setUniform` each on the cached
// uniform handles. Pure function — unit-testable without bgfx.
export [[nodiscard]] auto packDirectionalLightUniform(const DirectionalLight& light) noexcept
    -> std::array<glm::vec4, 2> {
    const glm::vec3 dir = glm::normalize(light.direction);
    return {
        glm::vec4{dir, light.intensity},
        glm::vec4{light.color, 0.0F},
    };
}

// Pack a PointLight into two vec4s:
//   u_pointLightPosition = vec4(position, range)
//   u_pointLightColor    = vec4(color, intensity)
export [[nodiscard]] auto packPointLightUniform(const PointLight& light) noexcept
    -> std::array<glm::vec4, 2> {
    return {
        glm::vec4{light.position, light.range},
        glm::vec4{light.color, light.intensity},
    };
}

// Uploads the first light of each supported type. Missing lights are
// explicitly disabled so removing a point light during editor preview
// cannot leave the previous frame's uniforms active.
export auto uploadLights(const World& world, const LightUniforms& uniforms) -> void {
    const auto& reg = world.registry();
    auto directional = packDirectionalLightUniform(DirectionalLight{.intensity = 0.0F});
    for (const auto e : reg.view<const DirectionalLight>()) {
        directional = packDirectionalLightUniform(reg.get<const DirectionalLight>(e));
        break;
    }
    auto point = packPointLightUniform(PointLight{.intensity = 0.0F});
    for (const auto e : reg.view<const PointLight>()) {
        point = packPointLightUniform(reg.get<const PointLight>(e));
        break;
    }
    bgfx::setUniform(uniforms.dir, directional.data());
    bgfx::setUniform(uniforms.color, &directional[1]);
    bgfx::setUniform(uniforms.pointPosition, point.data());
    bgfx::setUniform(uniforms.pointColor, &point[1]);
}

} // namespace roboslop
