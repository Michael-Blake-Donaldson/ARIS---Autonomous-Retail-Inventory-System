execute_process(
    COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom --explore-steps 80
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 10
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Exploration failed: ${status} ${error}")
endif()
string(JSON method GET "${output}" method)
string(JSON plans GET "${output}" plans)
string(JSON free GET "${output}" free)
if(NOT method STREQUAL "frontier exploration" OR NOT plans GREATER 0 OR NOT free GREATER 0)
    message(FATAL_ERROR "Unexpected exploration summary: ${output}")
endif()

function(reject expected_status expected_reason)
    execute_process(
        COMMAND "${ARGUS_EXECUTABLE}" --warehouse small-backroom ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 10
    )
    if(NOT status EQUAL expected_status OR NOT output STREQUAL "" OR
       NOT error MATCHES "\"code\":\"${expected_reason}\"")
        message(FATAL_ERROR "Unexpected rejection: ${status} ${output} ${error}")
    endif()
endfunction()

reject(2 invalid_explore_steps --explore-steps 2001)
reject(2 invalid_arguments --explore-steps-extra 80)
reject(2 invalid_dock --explore-steps 80 --dock-mm 1 -1)
reject(1 invalid_dock --explore-steps 80 --dock-mm 14000 1200)
reject(2 invalid_arguments --explore-steps 80 --map-steps 40)
reject(2 invalid_arguments --drive-steps 40 --dock-mm 3000 3000)
