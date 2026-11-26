cmake_minimum_required(VERSION 3.25)
project(ARGUS VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(ARGUS_ENABLE_SANITIZERS "Enable ASan and UBSan on project targets" OFF)
option(ARGUS_ENABLE_TSAN "Enable ThreadSanitizer on project targets" OFF)
option(ARGUS_ENABLE_FUZZING "Build Clang libFuzzer parser targets" OFF)
option(ARGUS_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
option(ARGUS_ENABLE_NATIVE_UI_TESTS "Run Windows operations center tests in an interactive session" OFF)
option(ARGUS_PACKAGE_WINDOWS "Enable the Windows x64 portable release package" OFF)
if(ARGUS_ENABLE_SANITIZERS AND
   (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|GNU)$"))
    message(FATAL_ERROR "ARGUS sanitizers require Clang or GCC on Linux")
endif()
if((ARGUS_ENABLE_TSAN OR ARGUS_ENABLE_FUZZING) AND
   (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    message(FATAL_ERROR "ARGUS fuzzing and ThreadSanitizer require Clang on Linux")
endif()
if((ARGUS_ENABLE_TSAN AND ARGUS_ENABLE_SANITIZERS) OR
   (ARGUS_ENABLE_FUZZING AND NOT ARGUS_ENABLE_SANITIZERS))
    message(FATAL_ERROR "Use separate TSan and ASan/UBSan builds and enable ASan/UBSan for fuzzing")
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
    "${ARGUS_FIXTURE_DIR}/rfid-audit.json"
)
file(READ "${ARGUS_FIXTURE_DIR}/small-backroom.json" ARGUS_SMALL_BACKROOM)
file(READ "${ARGUS_FIXTURE_DIR}/cluttered-receiving.json" ARGUS_CLUTTERED_RECEIVING)
file(READ "${ARGUS_FIXTURE_DIR}/broad-stockroom.json" ARGUS_BROAD_STOCKROOM)
file(READ "${ARGUS_FIXTURE_DIR}/rfid-audit.json" ARGUS_RFID_AUDIT)
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

add_library(argus_mapping STATIC src/mapping/grid.cpp src/mapping/mapper.cpp src/mapping/map_io.cpp
    src/mapping/map_file.cpp)
find_path(ARGUS_PICOSHA2_INCLUDE_DIR NAMES picosha2.h REQUIRED)
target_include_directories(argus_mapping SYSTEM PRIVATE "${ARGUS_PICOSHA2_INCLUDE_DIR}")
target_link_libraries(argus_mapping PUBLIC argus_core)
argus_project_target(argus_mapping)

add_library(argus_navigation STATIC src/navigation/planner.cpp src/navigation/navigator.cpp)
target_link_libraries(argus_navigation PUBLIC argus_mapping)
argus_project_target(argus_navigation)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
add_library(argus_inventory STATIC src/inventory/catalog.cpp src/inventory/import_path.cpp
    src/inventory/repository.cpp)
target_link_libraries(argus_inventory PUBLIC argus_core
    PRIVATE nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3)
argus_project_target(argus_inventory)

add_library(argus_audit STATIC src/audit/evidence.cpp src/audit/report.cpp)
target_link_libraries(argus_audit PUBLIC argus_inventory argus_navigation
    PRIVATE nlohmann_json::nlohmann_json)
argus_project_target(argus_audit)

add_library(argus_audit_sim STATIC src/audit/demo.cpp src/audit/stockroom.cpp)
target_link_libraries(argus_audit_sim PUBLIC argus_audit
    PRIVATE argus_sensors nlohmann_json::nlohmann_json)
argus_project_target(argus_audit_sim)

add_library(argus_mission STATIC src/mission/model.cpp src/mission/store.cpp)
target_link_libraries(argus_mission PUBLIC argus_inventory PRIVATE unofficial::sqlite3::sqlite3)
argus_project_target(argus_mission)

add_library(argus_operations_model STATIC apps/argus-desktop/operations_model.cpp
    apps/argus-desktop/occupancy_mesh.cpp apps/argus-desktop/fleet_report.cpp)
target_include_directories(argus_operations_model PUBLIC apps/argus-desktop)
target_link_libraries(argus_operations_model PUBLIC argus_navigation
    PRIVATE argus_inventory nlohmann_json::nlohmann_json)
argus_project_target(argus_operations_model)

add_library(argus_mission_sim STATIC src/mission/simulation.cpp)
target_link_libraries(argus_mission_sim PUBLIC argus_mission
    PRIVATE argus_audit_sim argus_sensors)
argus_project_target(argus_mission_sim)

add_executable(argus-mission apps/argus-mission/main.cpp)
target_link_libraries(argus-mission PRIVATE argus_mission_sim nlohmann_json::nlohmann_json)
argus_project_target(argus-mission)

add_executable(argus-inventory apps/argus-inventory/main.cpp)
target_link_libraries(argus-inventory PRIVATE argus_audit_sim nlohmann_json::nlohmann_json)
argus_project_target(argus-inventory)

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
    enable_language(RC)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory/fictional-catalog.csv")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory/fictional-catalog.csv"
        ARGUS_OPERATIONS_CATALOG)
    configure_file(cmake/operations_fixture.hpp.in generated/operations_fixture.hpp @ONLY)
    configure_file(cmake/fleet_photos.rc.in generated/fleet_photos.rc @ONLY)
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/fleet_photos.rc"
        APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory/photos/990-01-0001.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory/photos/990-02-0001.bmp"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory/photos/990-03-0001.bmp")
    add_executable(argus-desktop apps/argus-desktop/main.cpp apps/argus-desktop/live_engine.cpp
        apps/argus-desktop/panels.cpp apps/argus-desktop/fleet.cpp
        apps/argus-desktop/fleet_photos.cpp apps/argus-desktop/fleet_panel.cpp
        apps/argus-desktop/scene.cpp apps/argus-desktop/workbench.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/fleet_photos.rc")
    target_compile_definitions(argus-desktop PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(argus-desktop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(argus-desktop PRIVATE argus_app argus_operations_model argus_navigation argus_mission_sim
        argus_audit_sim SDL3::SDL3 OpenGL::GL imgui::imgui nlohmann_json::nlohmann_json)
    argus_project_target(argus-desktop)
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_executable(argus_unit_tests tests/unit/core_test.cpp tests/unit/options_test.cpp
        tests/unit/geometry_test.cpp tests/unit/warehouse_test.cpp tests/unit/robot_test.cpp
        tests/unit/sensors_test.cpp tests/unit/mapping_test.cpp tests/unit/map_io_test.cpp
        tests/unit/navigation_test.cpp tests/unit/inventory_test.cpp tests/unit/audit_test.cpp
        tests/unit/mission_test.cpp tests/unit/operations_test.cpp tests/unit/stockroom_test.cpp)
    target_link_libraries(argus_unit_tests PRIVATE argus_app argus_sensors argus_mapping
        argus_navigation argus_inventory argus_audit_sim argus_mission argus_operations_model
        unofficial::sqlite3::sqlite3 GTest::gtest_main)
    argus_project_target(argus_unit_tests)
    gtest_discover_tests(argus_unit_tests DISCOVERY_MODE PRE_TEST)

    add_test(NAME argus-inventory.cli COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-inventory>"
        "-DARGUS_CATALOG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory"
        "-DARGUS_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/inventory-cli-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/inventory_cli.cmake")
    add_test(NAME argus-inventory.audit COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-inventory>"
        "-DARGUS_CATALOG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory"
        "-DARGUS_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/audit-cli-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/audit_cli.cmake")
    add_test(NAME argus-mission.cli COMMAND "${CMAKE_COMMAND}"
        "-DARGUS_EXECUTABLE=$<TARGET_FILE:argus-mission>"
        "-DARGUS_INVENTORY=$<TARGET_FILE:argus-inventory>"
        "-DARGUS_CATALOG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/assets/inventory"
        "-DARGUS_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/mission-cli-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/mission_cli.cmake")
    set_tests_properties(argus-mission.cli PROPERTIES TIMEOUT 90)

    add_executable(argus-map-evaluation tests/evaluation/map_evaluation.cpp)
    target_link_libraries(argus-map-evaluation PRIVATE argus_mapping argus_sensors)
    argus_project_target(argus-map-evaluation)
    add_test(NAME argus-map-evaluation COMMAND argus-map-evaluation)

    add_executable(argus-navigation-evaluation tests/evaluation/navigation_evaluation.cpp)
    target_link_libraries(argus-navigation-evaluation PRIVATE argus_navigation argus_sensors)
    argus_project_target(argus-navigation-evaluation)
    add_test(NAME argus-navigation-evaluation COMMAND argus-navigation-evaluation)

    add_executable(argus-scale-evaluation tests/evaluation/scale_evaluation.cpp)
    target_link_libraries(argus-scale-evaluation PRIVATE argus_mapping argus_sensors
        nlohmann_json::nlohmann_json)
    argus_project_target(argus-scale-evaluation)
    add_test(NAME argus-scale-evaluation COMMAND argus-scale-evaluation 10000 200)
    set_tests_properties(argus-scale-evaluation PROPERTIES TIMEOUT 90)

    add_executable(argus-occupancy-evaluation tests/evaluation/occupancy_evaluation.cpp)
    target_link_libraries(argus-occupancy-evaluation PRIVATE argus_operations_model
        nlohmann_json::nlohmann_json)
    argus_project_target(argus-occupancy-evaluation)

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
        if(ARGUS_ENABLE_NATIVE_UI_TESTS)
            add_test(NAME argus-desktop.operations COMMAND argus-desktop --ui-smoke)
            set_tests_properties(argus-desktop.operations PROPERTIES TIMEOUT 70 RUN_SERIAL TRUE)
            add_test(NAME argus-desktop.fleet
                COMMAND ${CMAKE_COMMAND} "-DARGUS_DESKTOP=$<TARGET_FILE:argus-desktop>"
                    "-DARGUS_FLEET_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/fleet_smoke.cmake")
            set_tests_properties(argus-desktop.fleet PROPERTIES TIMEOUT 85 RUN_SERIAL TRUE)
        endif()
    endif()
endif()

if(ARGUS_ENABLE_FUZZING)
    foreach(parser IN ITEMS scenario catalog map rfid)
        add_executable(argus-fuzz-${parser} tests/fuzz/${parser}.cpp)
        argus_project_target(argus-fuzz-${parser})
        target_link_options(argus-fuzz-${parser} PRIVATE -fsanitize=fuzzer)
    endforeach()
    target_link_libraries(argus-fuzz-scenario PRIVATE argus_world)
    target_link_libraries(argus-fuzz-catalog PRIVATE argus_inventory)
    target_link_libraries(argus-fuzz-map PRIVATE argus_mapping)
    target_link_libraries(argus-fuzz-rfid PRIVATE argus_audit)
endif()

if(ARGUS_PACKAGE_WINDOWS)
    include(cmake/WindowsPackage.cmake)
endif()
