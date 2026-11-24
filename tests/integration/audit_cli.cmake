file(MAKE_DIRECTORY "${ARGUS_TEST_DIR}")
set(database "${ARGUS_TEST_DIR}/audit.db")
file(REMOVE "${database}")

function(inventory expected_status)
    execute_process(
        COMMAND "${ARGUS_EXECUTABLE}" "${database}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE
        TIMEOUT 15
    )
    if(NOT status STREQUAL expected_status)
        message(FATAL_ERROR "Inventory command failed with '${status}': ${error}")
    endif()
    set(report "${output}" PARENT_SCOPE)
    set(diagnostic "${error}" PARENT_SCOPE)
endfunction()

inventory(0 import "${ARGUS_CATALOG_DIR}" fictional-catalog.csv)
inventory(0 audit SOFTLINES 801)
string(JSON raw GET "${report}" missionRawReads)
string(JSON unique GET "${report}" missionUniqueObserved)
string(JSON baseline GET "${report}" firstPassResolved)
string(JSON resolved GET "${report}" resolvedAfterSearch)
string(JSON missing GET "${report}" items 2 finding)
string(JSON misplaced GET "${report}" items 1 finding)
string(JSON candidate GET "${report}" items 1 candidateAisle)
string(JSON assigned GET "${report}" quantities 0 catalogAssignedEpcs)
string(JSON scoped GET "${report}" quantities 0 uniqueObservedInAisle)
string(JSON expected GET "${report}" quantities 0 expectedQuantity)
string(JSON first_frames GET "${report}" visits 0 qualifiedFrames)
string(JSON alternate_frames GET "${report}" visits 2 qualifiedFrames)
if(NOT unique EQUAL 3 OR NOT raw GREATER unique OR NOT baseline EQUAL 0 OR NOT resolved EQUAL 2 OR
   NOT first_frames GREATER_EQUAL 8 OR NOT alternate_frames GREATER_EQUAL 8 OR
   NOT missing STREQUAL "not_observed_after_two_views" OR
   NOT misplaced STREQUAL "likely_misplaced" OR NOT candidate STREQUAL "TECH" OR
   NOT assigned EQUAL 2 OR NOT scoped EQUAL 1 OR NOT expected EQUAL 4)
    message(FATAL_ERROR "Unexpected seeded audit results: ${report}")
endif()

inventory(0 find-item 990-01-0001 802 TECH)
string(JSON first_aisle GET "${report}" visits 0 aisle)
string(JSON item_count LENGTH "${report}" items)
if(NOT first_aisle STREQUAL "TECH" OR NOT item_count EQUAL 2)
    message(FATAL_ERROR "Find Item did not prioritize the hint or restrict the report: ${report}")
endif()

inventory(0 audit SOFTLINES 803 csv)
if(NOT report MATCHES "case_id,epc" OR NOT report MATCHES "likely_misplaced" OR
   NOT report MATCHES "not_observed_after_two_views" OR
   NOT report MATCHES "review_disposition,review_notes" OR
   NOT report MATCHES "pending" OR report MATCHES "observed_in_assigned_aisle")
    message(FATAL_ERROR "CSV did not contain only review cases: ${report}")
endif()

inventory(1 audit SOFTLINES 801)
if(NOT diagnostic STREQUAL "argus-inventory: duplicate")
    message(FATAL_ERROR "A reused session should be rejected before scanning")
endif()
inventory(2 find-item 990-01-0001 9223372036854775808)
inventory(2 audit SOFTLINES 804 invalid-format)

inventory(0 find 990-01-0001)
string(JSON item_count LENGTH "${report}" units)
if(NOT item_count EQUAL 2)
    message(FATAL_ERROR "Audit changed catalog assignments: ${report}")
endif()
