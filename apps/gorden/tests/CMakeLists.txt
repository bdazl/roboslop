find_package(Catch2 3 CONFIG REQUIRED)

add_executable(gorden_tests
    interface_test.cpp
    first_room_test.cpp
    player_test.cpp
    memory_test.cpp
    tools_test.cpp
    observation_test.cpp
    robot_test.cpp
    brain_test.cpp
    save_test.cpp
    scene_asset_test.cpp
    settings_test.cpp
    llm_config_test.cpp
)

# Same language baseline as the engine so the imported PCMs match.
roboslop_apply_language_defaults(gorden_tests)

target_link_libraries(gorden_tests PRIVATE
    gorden_agent
    roboslop
    Catch2::Catch2WithMain
)

target_compile_definitions(gorden_tests PRIVATE
    GORDEN_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
)

roboslop_set_warnings(gorden_tests)
roboslop_apply_sanitizers(gorden_tests)

include(Catch)
catch_discover_tests(gorden_tests)
