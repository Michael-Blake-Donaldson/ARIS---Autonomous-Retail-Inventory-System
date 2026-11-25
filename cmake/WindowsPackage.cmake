if(NOT WIN32 OR NOT MSVC OR NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows" OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT BUILD_TESTING)
    message(FATAL_ERROR "The portable package requires the Windows x64 MSVC Release test build")
endif()

install(TARGETS argus-desktop argus-headless argus-inventory argus-mission RUNTIME DESTINATION bin)

set(port_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
install(FILES "${port_root}/bin/SDL3.dll" "${port_root}/bin/fmt.dll"
    "${port_root}/bin/spdlog.dll" "${port_root}/bin/sqlite3.dll" DESTINATION bin)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    message(FATAL_ERROR "The MSVC app-local redistributables were not found")
endif()
install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin)

foreach(port IN ITEMS fmt imgui nlohmann-json picosha2 sdl3 spdlog sqlite3)
    install(FILES "${port_root}/share/${port}/copyright"
        DESTINATION "licenses/${port}")
endforeach()
install(FILES README.md CHANGELOG.md docs/release/NOTICE.md DESTINATION .)
install(DIRECTORY assets/inventory assets/warehouse DESTINATION examples
    FILES_MATCHING PATTERN "*.json" PATTERN "*.csv")
install(DIRECTORY docs/ DESTINATION docs FILES_MATCHING
    PATTERN "*.md" PATTERN "evidence" EXCLUDE)
install(FILES docs/testing/evidence/phase-10-operations-center.png DESTINATION docs/testing/evidence)
install(DIRECTORY "${CMAKE_BINARY_DIR}/release-evidence/" DESTINATION verification)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "ARGUS-${PROJECT_VERSION}-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_VERBATIM_VARIABLES YES)
include(CPack)
