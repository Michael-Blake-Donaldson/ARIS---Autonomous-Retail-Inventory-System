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
        NOT output MATCHES "\"auditContextRestored\":true" OR
        NOT output MATCHES "\"holdVisible\":true" OR
        NOT output MATCHES "\"returnedToBerth\":true" OR
        NOT output MATCHES "\"auditSavedAfterHold\":true" OR
        NOT output MATCHES "\"focusLossHeldFleet\":true"))
        message(FATAL_ERROR "Dashboard review evidence is incomplete\n${output}")
    endif()
    message(STATUS "${output}")
endforeach()
foreach(mode IN ITEMS review review-reopen)
    execute_process(COMMAND "${ARGUS_DESKTOP}" "--ui-fleet-${mode}" "${data}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 35)
    if(mode STREQUAL "review-reopen")
        set(reopened true)
    else()
        set(reopened false)
    endif()
    if(NOT result STREQUAL "0" OR NOT output MATCHES "\"reviewed\":true" OR
       NOT output MATCHES "\"reviewFilter\":true" OR
       NOT output MATCHES "\"escapeKeptSession\":true" OR
       NOT output MATCHES "\"recheckLinked\":true" OR
       NOT output MATCHES "\"comparisonVisible\":true" OR
       NOT output MATCHES "\"reopened\":${reopened}")
        message(FATAL_ERROR "Audit ${mode} failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
    endif()
    message(STATUS "${output}")
endforeach()
execute_process(COMMAND "${ARGUS_DESKTOP}" --ui-fleet-keyboard "${data}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 15)
if(NOT result STREQUAL "0" OR NOT output MATCHES "\"keyboardOnly\":true" OR
    NOT output MATCHES "\"mistypeRecovered\":true" OR
    NOT output MATCHES "\"reportSelectionPreserved\":true")
    message(FATAL_ERROR "Keyboard audit failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
endif()
message(STATUS "${output}")
file(REMOVE_RECURSE "${data}")
set(data "${ARGUS_FLEET_ROOT}/fleet-traffic-${run}")
file(MAKE_DIRECTORY "${data}")
execute_process(COMMAND "${ARGUS_DESKTOP}" --ui-fleet-traffic "${data}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 45)
if(NOT result STREQUAL "0" OR NOT output MATCHES "\"sharedFleet\":true" OR
    NOT output MATCHES "\"yielded\":true" OR NOT output MATCHES "\"bothAuditsCompleted\":true" OR
    NOT output MATCHES "\"firstRobot\":1" OR
    NOT output MATCHES "\"secondLocation\":\"A20J07\"")
    message(FATAL_ERROR "Shared aisle failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
endif()
message(STATUS "${output}")
file(REMOVE_RECURSE "${data}")

set(data "${ARGUS_FLEET_ROOT}/fleet-reverse-${run}")
file(MAKE_DIRECTORY "${data}")
execute_process(COMMAND "${ARGUS_DESKTOP}" --ui-fleet-traffic-reverse "${data}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 45)
if(NOT result STREQUAL "0" OR NOT output MATCHES "\"sharedFleet\":true" OR
    NOT output MATCHES "\"yielded\":true" OR NOT output MATCHES "\"bothAuditsCompleted\":true" OR
    NOT output MATCHES "\"firstRobot\":2")
    message(FATAL_ERROR "Reverse shared aisle failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
endif()
message(STATUS "${output}")
file(REMOVE_RECURSE "${data}")

set(data "${ARGUS_FLEET_ROOT}/fleet-fault-${run}")
file(MAKE_DIRECTORY "${data}")
set(journal "deliberately damaged mission database")
file(WRITE "${data}/missions.db" "${journal}")
execute_process(COMMAND "${ARGUS_DESKTOP}" --ui-fleet-data-fault "${data}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 20)
file(READ "${data}/missions.db" remaining)
if(NOT result STREQUAL "0" OR NOT output MATCHES "\"storageIssueVisible\":true" OR
    NOT output MATCHES "\"writesBlocked\":true" OR NOT output MATCHES "\"simulationAvailable\":true" OR
    NOT remaining STREQUAL journal)
    message(FATAL_ERROR "Damaged journal recovery failed (${result})\n${output}\n${diagnostics}\nData: ${data}")
endif()
message(STATUS "${output}")
file(REMOVE_RECURSE "${data}")
