module;
#include <bgfx/bgfx.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.scene.runtime;
import roboslop.assets.mesh;
import roboslop.assets.texture;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.render.asset_cache;
import roboslop.render.mesh;
import roboslop.render.material;
import roboslop.render.model;
import roboslop.render.primitives;
import roboslop.render.lighting;
import roboslop.scene.document;
import roboslop.scene.transform;

namespace roboslop {
export struct SceneIdentity {
    std::string id;
    std::string name;
};

namespace detail {
static auto solidTexture(const glm::vec3& color) -> bgfx::TextureHandle {
    const std::array<std::uint8_t, 4> pixel{
        static_cast<std::uint8_t>(color.x * 255),
        static_cast<std::uint8_t>(color.y * 255),
        static_cast<std::uint8_t>(color.z * 255),
        255
    };
    return bgfx::createTexture2D(
        1,
        1,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        0,
        bgfx::copy(pixel.data(), static_cast<std::uint32_t>(pixel.size()))
    );
}
} // namespace detail

// Owns shared primitive buffers, solid material textures and the GPU
// side of every model file a scene has referenced. Lives in the world's
// context (destroyed before bgfx shutdown). Entity handles only borrow.
export class SceneRuntime {
  public:
    SceneRuntime() = default;
    SceneRuntime(const SceneRuntime&) = delete;
    auto operator=(const SceneRuntime&) -> SceneRuntime& = delete;
    // Lives in the world's context, constructed in place: never moved.
    SceneRuntime(SceneRuntime&&) = delete;
    auto operator=(SceneRuntime&&) -> SceneRuntime& = delete;

    ~SceneRuntime() {
        destroyTextures();
        for ([[maybe_unused]] const auto& [name, mesh] : meshes) {
            bgfx::destroy(mesh.vb);
            bgfx::destroy(mesh.ib);
        }
        for ([[maybe_unused]] const auto& [path, model] : models) {
            for (const auto& part : model.parts) {
                bgfx::destroy(part.mesh.vb);
                bgfx::destroy(part.mesh.ib);
            }
            for (const auto handle : model.solidTextures) {
                bgfx::destroy(handle);
            }
        }
    }

    auto clear(World& world) -> void {
        for (const auto entity : entities) {
            if (world.valid(entity)) {
                releasePhysicsBody(world, entity);
                world.destroy(entity);
            }
        }
        entities.clear();
        destroyTextures();
    }

    // Model-space bounds of a model file this runtime has loaded, for
    // picking and other CPU-side queries. Empty until `replace` or
    // `instantiateModel` has loaded the path.
    [[nodiscard]] auto modelBounds(std::string_view path) const -> std::optional<Aabb> {
        if (const auto it = models.find(std::string{path}); it != models.end()) {
            return it->second.bounds;
        }
        return std::nullopt;
    }

    // A render-only instance for app-owned actors. This runtime owns the
    // cached buffers/textures; clear/replace only destroys scene entities,
    // so actor instances remain valid across save/load scene replacement.
    [[nodiscard]] auto instantiateModel(AssetCache& assets, const std::string& path)
        -> Result<ModelInstance> {
        if (!assetPathValid(path)) {
            return std::unexpected(sceneError("invalid model path: " + path));
        }
        if (!models.contains(path)) {
            auto program = assets.program("vs_scene", "fs_scene");
            if (!program) {
                return std::unexpected(program.error());
            }
            auto loaded = loadModel(assets, path, *program, assets.sampler("s_albedo"));
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            models.emplace(path, std::move(*loaded));
        }
        return ModelInstance{models.at(path).parts};
    }

    [[nodiscard]] auto
    replace(World& world, AssetCache& assets, const SceneDocument& document, bool simulate)
        -> Result<void> {
        if (auto valid = validateScene(document); !valid) {
            return valid;
        }
        auto program = assets.program("vs_scene", "fs_scene");
        if (!program) {
            return std::unexpected(program.error());
        }
        if (meshes.empty()) {
            meshes.emplace("cube", makeGeometryMesh(cubeGeometry()));
            meshes.emplace("sphere", makeGeometryMesh(sphereGeometry(24, 32, 0.5F)));
            meshes.emplace("plane", makeGeometryMesh(planeGeometry(1, 1)));
        }
        const auto sampler = assets.sampler("s_albedo");
        // Load model files before touching the world so a bad reference
        // leaves the previous scene standing.
        for (const auto& object : document.objects) {
            if (object.geometry == "model" && !models.contains(object.model)) {
                auto loaded = loadModel(assets, object.model, *program, sampler);
                if (!loaded) {
                    return std::unexpected(loaded.error());
                }
                models.emplace(object.model, std::move(*loaded));
            }
        }
        clear(world);
        for (const auto& [id, color] : document.materials) {
            textures.emplace(id, detail::solidTexture(color));
        }
        for (const auto& object : document.objects) {
            const auto entity = world.create();
            entities.push_back(entity);
            world.emplace<SceneIdentity>(
                entity, SceneIdentity{.id = object.id, .name = object.name}
            );
            world.emplace<Transform>(entity, object.transform);
            if (object.geometry == "model") {
                const auto& model = models.at(object.model);
                world.emplace<ModelInstance>(entity, ModelInstance{model.parts});
                if (simulate && object.body != "none") {
                    const auto extent = model.bounds.max - model.bounds.min;
                    const auto center = (model.bounds.max + model.bounds.min) * 0.5F;
                    world.emplace<BodyDesc>(
                        entity,
                        BodyDesc{
                            .shape =
                                BoxShape{
                                    .halfExtents = extent * 0.5F * object.transform.scale,
                                    .center = center * object.transform.scale
                                },
                            .motion = motionOf(object.body)
                        }
                    );
                }
                continue;
            }
            auto mesh = meshes.at(object.geometry);
            mesh.program = program->value;
            world.emplace<Mesh>(entity, mesh);
            world.emplace<Material>(
                entity,
                Material{
                    .program = *program, .albedo = textures.at(object.material), .sAlbedo = sampler
                }
            );
            if (simulate && object.body != "none") {
                BodyDesc body;
                body.motion = motionOf(object.body);
                if (object.geometry == "sphere") {
                    body.shape = SphereShape{object.transform.scale.x * 0.5F};
                } else {
                    body.shape = BoxShape{.halfExtents = object.transform.scale * 0.5F};
                }
                world.emplace<BodyDesc>(entity, body);
            }
        }
        const auto light = world.create();
        entities.push_back(light);
        world.emplace<DirectionalLight>(light, document.light);
        if (document.pointLight) {
            const auto pointLight = world.create();
            entities.push_back(pointLight);
            world.emplace<PointLight>(pointLight, *document.pointLight);
        }
        return {};
    }

  private:
    struct GpuModel {
        Aabb bounds;
        std::vector<ModelDrawPart> parts;
        std::vector<Texture> textures;                  // decoded images
        std::vector<bgfx::TextureHandle> solidTextures; // base colour fallbacks
    };

    static auto motionOf(const std::string& body) -> BodyMotion {
        return body == "static" ? BodyMotion::Static : BodyMotion::Dynamic;
    }

    // Uploads one model file: one texture per material (packed image,
    // image file next to the model, or a 1x1 base colour) and one
    // static mesh per part. Only base colour reaches the GPU; fs_scene
    // samples the albedo alone.
    [[nodiscard]] static auto loadModel(
        AssetCache& assets,
        const std::string& path,
        ProgramHandle program,
        bgfx::UniformHandle sampler
    ) -> Result<GpuModel> {
        const auto file = assets.root() / std::filesystem::path{path};
        auto asset = loadModelFile(file);
        if (!asset) {
            return std::unexpected(asset.error());
        }
        GpuModel model;
        model.bounds = roboslop::modelBounds(*asset);
        std::vector<bgfx::TextureHandle> albedo;
        albedo.reserve(asset->materials.size());
        for (const auto& material : asset->materials) {
            std::optional<Result<Texture>> texture;
            if (!material.textureData.empty()) {
                texture = loadTexture2D(std::span{material.textureData}, path);
            } else if (!material.texturePath.empty()) {
                texture = loadTexture2D(file.parent_path() / material.texturePath);
            }
            if (!texture) {
                const auto handle = detail::solidTexture(material.baseColor);
                model.solidTextures.push_back(handle);
                albedo.push_back(handle);
                continue;
            }
            if (!*texture) {
                return std::unexpected(texture->error());
            }
            albedo.push_back((*texture)->bgfxHandle());
            model.textures.push_back(std::move(**texture));
        }
        for (const auto& part : asset->parts) {
            auto mesh = makeStaticMesh(
                std::as_bytes(std::span{part.mesh.vertices}),
                std::span{part.mesh.indices},
                vertexLayoutPosNormalUv()
            );
            mesh.program = program.value;
            model.parts.push_back(
                ModelDrawPart{
                    .mesh = mesh,
                    .material =
                        Material{
                            .program = program,
                            .albedo = albedo.at(part.material),
                            .sAlbedo = sampler
                        },
                    .local = part.transform
                }
            );
        }
        return model;
    }

    auto destroyTextures() -> void {
        for ([[maybe_unused]] const auto& [id, handle] : textures) {
            bgfx::destroy(handle);
        }
        textures.clear();
    }

    std::vector<Entity> entities;
    std::map<std::string, Mesh> meshes;
    std::map<std::string, bgfx::TextureHandle> textures;
    std::map<std::string, GpuModel> models;
};
} // namespace roboslop
