if(NOT DEFINED ARGUS_DESKTOP OR NOT DEFINED ARGUS_FLEET_ROOT)
    message(FATAL_ERROR "Fleet smoke requires the desktop executable and a build directory")
endif()

string(RANDOM LENGTH 20 ALPHABET 0123456789abcdef run)
set(data "${ARGUS_FLEET_ROOT}/fleet-smoke-${run}")
file(MAKE_DIRECTORY "${data}")
foreach(mode IN ITEMS smoke reopen)
    execute_process(COMMAND "${ARGUS_DESKTOP}" "--ui-fleet-${mode}" "${data}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 40)
    if(NOT result STREQUAL "0" OR NOT output MATCHES "\"fleetAudits\":true" OR
       NOT output MATCHES "\"mixedLocations\":true" OR
       NOT output MATCHES "\"auditableLocations\":21")
        message(FATAL_ERROR "Fleet ${mode} failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
    endif()
    if(mode STREQUAL "reopen" AND
       (NOT output MATCHES "\"dashboard1280x720\":true" OR
        NOT output MATCHES "\"shelfAuditClicks\":2" OR
        NOT output MATCHES "\"selectionPreserved\":true" OR
        NOT output MATCHES "\"holdVisible\":true"))
        message(FATAL_ERROR "Dashboard review evidence is incomplete\n${output}")
    endif()
    message(STATUS "${output}")
endforeach()
file(REMOVE_RECURSE "${data}")
set(data "${ARGUS_FLEET_ROOT}/fleet-traffic-${run}")
file(MAKE_DIRECTORY "${data}")
execute_process(COMMAND "${ARGUS_DESKTOP}" --ui-fleet-traffic "${data}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 45)
if(NOT result STREQUAL "0" OR NOT output MATCHES "\"sharedFleet\":true" OR
    NOT output MATCHES "\"yielded\":true" OR NOT output MATCHES "\"bothAuditsCompleted\":true")
    message(FATAL_ERROR "Shared aisle failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
endif()
message(STATUS "${output}")
file(REMOVE_RECURSE "${data}")
