function(check_cli expected_status expected_stdout expected_stderr)
    execute_process(
        COMMAND "${ARGUS_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
        TIMEOUT 5
    )
    if(NOT status STREQUAL expected_status OR
       NOT stdout STREQUAL expected_stdout OR NOT stderr STREQUAL expected_stderr)
        message(FATAL_ERROR "${ARGUS_APP} ${ARGN}: exit '${status}', stdout '${stdout}', stderr '${stderr}'")
    endif()
endfunction()

check_cli(0 "ARGUS ${ARGUS_VERSION}" "" --version)
check_cli(0 "Usage: ${ARGUS_APP} [--help | --version | --seed UINT32 | --warehouse NAME [--tick UINT64] [--drive-steps N | --sensor-steps N] | --scenario-stdin [--tick UINT64] [--drive-steps N | --sensor-steps N] | --export-warehouse NAME]\nMapping: --warehouse NAME [--tick UINT64] [--cell-mm N] (--map-steps N | --export-map N | --map-stdin)\n  --export-map N [--map-output DIRECTORY NAME.argmap]\n  --map-stdin [--map-sha256 HEX]\nExploration: --warehouse NAME [--tick UINT64] [--cell-mm N] --explore-steps N [--dock-mm X Y]" "" --help)
check_cli(2 "" "{\"event\":\"configuration_rejected\",\"app\":\"${ARGUS_APP}\",\"code\":\"invalid_seed\"}"
    --seed 4294967296)
check_cli(2 "" "{\"event\":\"configuration_rejected\",\"app\":\"${ARGUS_APP}\",\"code\":\"invalid_arguments\"}"
    --unknown)

if(ARGUS_APP STREQUAL "argus-headless")
    set(sample [=[{"seed":5489,"firstSample":3499211612}]=])
    set(start "{\"event\":\"application_starting\",\"app\":\"argus-headless\",\"version\":\"${ARGUS_VERSION}\",\"seed\":5489}")
    check_cli(0 "${sample}" "${start}")
    check_cli(0 "${sample}" "${start}" --seed 5489)
    check_cli(0 [=[{"seed":0,"firstSample":2357136044}]=]
        "{\"event\":\"application_starting\",\"app\":\"argus-headless\",\"version\":\"${ARGUS_VERSION}\",\"seed\":0}"
        --seed 0)
else()
    check_cli(2 "" "argus-desktop: Choose an existing operations data directory."
        --operations-data "${CMAKE_CURRENT_LIST_FILE}")
    set(ENV{SDL_VIDEO_DRIVER} "argus-unsupported-driver")
    set(start "{\"event\":\"application_starting\",\"app\":\"argus-desktop\",\"version\":\"${ARGUS_VERSION}\",\"seed\":5489}")
    set(failure [=[{"event":"application_failed","app":"argus-desktop","code":"video_initialization"}]=])
    check_cli(1 "" "${start}\n${failure}")
endif()
