cmake_minimum_required(VERSION 3.25)
project(ARGUS VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(ARGUS_ENABLE_SANITIZERS "Enable ASan and UBSan on project targets" OFF)
option(ARGUS_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(ARGUS_ENABLE_SANITIZERS AND
   (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|GNU)$"))
    message(FATAL_ERROR "ARGUS sanitizers require Clang or GCC on Linux")
endif()
if(ARGUS_ENABLE_CLANG_TIDY)
    find_program(ARGUS_CLANG_TIDY NAMES clang-tidy-18 clang-tidy REQUIRED)
endif()
include(cmake/ProjectChecks.cmake)

add_library(argus_core STATIC src/core/random.cpp)
target_include_directories(argus_core PUBLIC include)
target_compile_features(argus_core PUBLIC cxx_std_20)
argus_project_target(argus_core)

find_package(nlohmann_json CONFIG REQUIRED)
set(ARGUS_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/warehouse")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ARGUS_FIXTURE_DIR}/small-backroom.json"
    "${ARGUS_FIXTURE_DIR}/cluttered-receiving.json"
    "${ARGUS_FIXTURE_DIR}/broad-stockroom.json"
)
file(READ "${ARGUS_FIXTURE_DIR}/small-backroom.json" ARGUS_SMALL_BACKROOM)
file(READ "${ARGUS_FIXTURE_DIR}/cluttered-receiving.json" ARGUS_CLUTTERED_RECEIVING)
file(READ "${ARGUS_FIXTURE_DIR}/broad-stockroom.json" ARGUS_BROAD_STOCKROOM)
configure_file(cmake/warehouse_fixtures.hpp.in generated/warehouse_fixtures.hpp @ONLY)
add_library(argus_world STATIC
    src/world/fixtures.cpp src/world/geometry.cpp src/world/motion.cpp
    src/world/scenario.cpp src/world/warehouse.cpp)
target_include_directories(argus_world PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(argus_world PUBLIC argus_core PRIVATE nlohmann_json::nlohmann_json)
argus_project_target(argus_world)

add_library(argus_robot STATIC src/robot/encoders.cpp src/robot/robot.cpp)
target_link_libraries(argus_robot PUBLIC argus_world)
argus_project_target(argus_robot)

add_library(argus_sensors STATIC src/sensors/lidar.cpp src/sensors/rfid.cpp src/sensors/sensors.cpp)
target_link_libraries(argus_sensors PUBLIC argus_robot)
argus_project_target(argus_sensors)

add_library(argus_mapping STATIC src/mapping/grid.cpp src/mapping/mapper.cpp src/mapping/map_io.cpp)
target_link_libraries(argus_mapping PUBLIC argus_core)
argus_project_target(argus_mapping)

add_library(argus_navigation STATIC src/navigation/planner.cpp src/navigation/navigator.cpp)
target_link_libraries(argus_navigation PUBLIC argus_mapping)
argus_project_target(argus_navigation)

find_package(spdlog CONFIG REQUIRED)
configure_file(cmake/version.hpp.in generated/version.hpp @ONLY)
add_library(argus_app STATIC
    src/app/application.cpp src/app/map_run.cpp src/app/options.cpp src/app/sensor_output.cpp
    src/app/warehouse_io.cpp)
target_include_directories(argus_app PUBLIC src/app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(argus_app PUBLIC argus_sensors argus_mapping PRIVATE argus_navigation spdlog::spdlog)
argus_project_target(argus_app)

add_executable(argus-headless apps/argus-headless/main.cpp)
target_link_libraries(argus-headless PRIVATE argus_app)
argus_project_target(argus-headless)

if(WIN32)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    add_executable(argus-desktop apps/argus-desktop/main.cpp apps/argus-desktop/warehouse_view.cpp)
    target_compile_definitions(argus-desktop PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(argus-desktop PRIVATE argus_app SDL3::SDL3 OpenGL::GL)
    argus_project_target(argus-desktop)
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_executable(argus_unit_tests tests/unit/core_test.cpp tests/unit/options_test.cpp
        tests/unit/geometry_test.cpp tests/unit/warehouse_test.cpp tests/unit/robot_test.cpp
        tests/unit/sensors_test.cpp tests/unit/mapping_test.cpp tests/unit/map_io_test.cpp
        tests/unit/navigation_test.cpp)
    target_link_libraries(argus_unit_tests PRIVATE argus_app argus_sensors argus_mapping
        argus_navigation GTest::gtest_main)
    argus_project_target(argus_unit_tests)
    gtest_discover_tests(argus_unit_tests DISCOVERY_MODE PRE_TEST)

    add_executable(argus-map-evaluation tests/evaluation/map_evaluation.cpp)
    target_link_libraries(argus-map-evaluation PRIVATE argus_mapping argus_sensors)
    argus_project_target(argus-map-evaluation)
    add_test(NAME argus-map-evaluation COMMAND argus-map-evaluation)

    add_executable(argus-navigation-evaluation tests/evaluation/navigation_evaluation.cpp)
    target_link_libraries(argus-navigation-evaluation PRIVATE argus_navigation argus_sensors)
    argus_project_target(argus-navigation-evaluation)
    add_test(NAME argus-navigation-evaluation COMMAND argus-navigation-evaluation)

    function(argus_cli_smoke target)
        add_test(NAME ${target}.cli COMMAND "${CMAKE_COMMAND}"
            "-DARGUS_EXECUTABLE=$<TARGET_FILE:${target}>"
            "-DARGUS_APP=${target}"
            "-DARGUS_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/cli_smoke.cmake"
        )
    endfunction()
    argus_cli_smoke(argus-headless)
    add_test(NAME argus-headless.warehouse COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-headless>"
        "-DARGUS_FIXTURE_DIR=${ARGUS_FIXTURE_DIR}"
        "-DARGUS_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/warehouse-cli-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/warehouse_cli.cmake"
    )
    add_test(NAME argus-headless.robot COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-headless>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/robot_cli.cmake"
    )
    add_test(NAME argus-headless.sensors COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-headless>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/sensor_cli.cmake"
    )
    add_test(NAME argus-headless.mapping COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-headless>"
        "-DARGUS_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/map-cli-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/map_cli.cmake"
    )
    add_test(NAME argus-headless.navigation COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-headless>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/navigation_cli.cmake"
    )
    if(TARGET argus-desktop)
        argus_cli_smoke(argus-desktop)
    endif()
endif()
