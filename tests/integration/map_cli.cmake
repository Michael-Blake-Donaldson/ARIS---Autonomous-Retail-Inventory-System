file(MAKE_DIRECTORY "${ARGUS_TEST_DIR}")
set(snapshot "${ARGUS_TEST_DIR}/map.argmap")
execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --map-steps 80 --cell-mm 300
    RESULT_VARIABLE status OUTPUT_VARIABLE summary ERROR_VARIABLE diagnostics TIMEOUT 8)
if(NOT status STREQUAL "0" OR summary MATCHES "truePose|fixtures|actors")
    message(FATAL_ERROR "Mapping replay failed or exposed geometry: ${status}, ${diagnostics}")
endif()
string(JSON method GET "${summary}" method)
string(JSON occupied GET "${summary}" occupied)
string(JSON free GET "${summary}" free)
string(JSON unknown GET "${summary}" unknown)
if(NOT method STREQUAL "occupancy mapping + odometry" OR occupied LESS 10 OR free LESS 50 OR unknown LESS 10)
    message(FATAL_ERROR "Unexpected map summary: ${summary}")
endif()

execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --export-map 80 --cell-mm 300
    RESULT_VARIABLE status OUTPUT_FILE "${snapshot}" ERROR_VARIABLE diagnostics TIMEOUT 8)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Map export failed: ${status}, ${diagnostics}")
endif()
execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --map-stdin --cell-mm 300
    INPUT_FILE "${snapshot}" RESULT_VARIABLE status OUTPUT_VARIABLE imported ERROR_VARIABLE diagnostics TIMEOUT 8)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Map import failed: ${status}, ${diagnostics}")
endif()
foreach(field IN ITEMS occupied free unknown)
    string(JSON actual GET "${imported}" "${field}")
    if(NOT actual EQUAL ${field})
        message(FATAL_ERROR "Map round-trip changed ${field}: ${actual}")
    endif()
endforeach()

execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --map-stdin
    INPUT_FILE "${snapshot}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 5)
if(NOT status STREQUAL "1" OR NOT output STREQUAL "" OR NOT diagnostics MATCHES "incompatible_map")
    message(FATAL_ERROR "Resolution mismatch was not rejected: ${status}, ${diagnostics}")
endif()

set(atomic "${ARGUS_TEST_DIR}/atomic.argmap")
file(WRITE "${atomic}" "previous snapshot")
execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --export-map 80 --cell-mm 300
    --map-output "${ARGUS_TEST_DIR}" atomic.argmap
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 8)
file(SHA256 "${atomic}" atomic_hash)
file(SHA256 "${snapshot}" stream_hash)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "" OR NOT atomic_hash STREQUAL stream_hash)
    message(FATAL_ERROR "Atomic export differed from the stream: ${status}, ${diagnostics}")
endif()

file(SIZE "${atomic}" size)
math(EXPR offset "${size} - 32")
file(READ "${atomic}" pin OFFSET ${offset} LIMIT 32 HEX)
foreach(candidate IN ITEMS "${pin}" "0000000000000000000000000000000000000000000000000000000000000000")
    execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --map-stdin --cell-mm 300
        --map-sha256 "${candidate}" INPUT_FILE "${atomic}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 8)
    if(candidate STREQUAL pin)
        if(NOT status STREQUAL "0" OR NOT output STREQUAL imported)
            message(FATAL_ERROR "Trusted map digest was rejected: ${status}, ${diagnostics}")
        endif()
    elseif(NOT status STREQUAL "1" OR NOT output STREQUAL "" OR NOT diagnostics MATCHES "incompatible_map")
        message(FATAL_ERROR "Wrong trusted digest was accepted: ${status}, ${diagnostics}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${ARGUS_TEST_DIR}/blocked.argmap")
foreach(leaf IN ITEMS "../escaped.argmap" "nul.argmap" "blocked.argmap")
    execute_process(COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --export-map 1
        --map-output "${ARGUS_TEST_DIR}" "${leaf}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 8)
    if(NOT status STREQUAL "1" OR NOT output STREQUAL "")
        message(FATAL_ERROR "Unsafe or blocked export was accepted: ${status}, ${diagnostics}")
    endif()
endforeach()
file(GLOB leftovers "${ARGUS_TEST_DIR}/.argus-map-*.tmp")
if(leftovers OR NOT IS_DIRECTORY "${ARGUS_TEST_DIR}/blocked.argmap")
    message(FATAL_ERROR "Failed map publishing changed the destination or left temporary files")
endif()
