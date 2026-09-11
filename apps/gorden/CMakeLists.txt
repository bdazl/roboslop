# Gameplay and agent logic (player, observation, memory, tools + validation,
# robot motion, brain) share a module library so gorden_tests can exercise them
# without the window/bgfx main.
roboslop_add_module_library(gorden_agent
    MODULES
        src/gameplay/interface.cppm
        src/gameplay/player.cppm
        src/gameplay/player_visual.cppm
        src/gameplay/robot_visual.cppm
        src/agent/observation.cppm
        src/agent/memory.cppm
        src/agent/tools.cppm
        src/agent/robot.cppm
        src/agent/brain.cppm
        src/agent/save.cppm
        src/agent/settings.cppm
        src/agent/llm_config.cppm
)

target_link_libraries(gorden_agent PUBLIC
    roboslop
)

add_executable(gorden
    src/app/main.cpp
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/scenes)
# The checked-in scene is authoritative. configure_file records the source as
# a CMake input, so a normal build reconfigures and refreshes the staged copy
# after room edits.
configure_file(assets/scenes/room.json ${CMAKE_BINARY_DIR}/assets/scenes/room.json COPYONLY)

# Models are authored in Blender, never in the build tree, so they are
# always refreshed. Keep their category directories in the build tree too.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/models)
file(GLOB_RECURSE gorden_model_sources CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/assets/models
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/*.glb
)
foreach(model_source IN LISTS gorden_model_sources)
    get_filename_component(model_directory ${model_source} DIRECTORY)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/models/${model_directory})
    configure_file(
        assets/models/${model_source}
        ${CMAKE_BINARY_DIR}/assets/models/${model_source}
        COPYONLY
    )
endforeach()

target_link_libraries(gorden PRIVATE
    roboslop
    gorden_agent
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
    # Needs a render context, so it stays out of CTest (as the editor's
    # runtime smoke test does).
    add_executable(gorden_save_smoke tests/save_smoke.cpp)
    target_link_libraries(gorden_save_smoke PRIVATE gorden_agent roboslop)
    roboslop_apply_language_defaults(gorden_save_smoke)
    roboslop_set_warnings(gorden_save_smoke)
    roboslop_apply_sanitizers(gorden_save_smoke)
endif()

roboslop_apply_language_defaults(gorden)
roboslop_set_warnings(gorden)
roboslop_apply_sanitizers(gorden)

include(ShaderCompile)

set(shader_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)

# Outputs land at <build-root>/assets/shaders/<backend>/ so the runtime
# asset path is stable across apps — gorden's main passes
# assetRoot = "assets" and the engine resolves shaders relative to that.
# `make run` cds into build/<preset>/ so the relative path works.
set(shader_out_dir ${CMAKE_BINARY_DIR}/assets/shaders)

roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${shader_src_dir}/vs_basic.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_basic_vs_target
)

roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${shader_src_dir}/fs_basic.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_basic_fs_target
)

roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${shader_src_dir}/vs_textured.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_textured_vs_target
)

roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${shader_src_dir}/fs_textured.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_textured_fs_target
)

# gorden loads these at startup; depending on them makes `make build`
# alone produce a runnable binary (the aggregate `shaders` target is
# not part of ALL).
add_dependencies(gorden
    ${gorden_basic_vs_target}
    ${gorden_basic_fs_target}
    ${gorden_textured_vs_target}
    ${gorden_textured_fs_target}
)
