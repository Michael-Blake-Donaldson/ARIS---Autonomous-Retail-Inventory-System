file(MAKE_DIRECTORY "${ARGUS_TEST_DIR}")
set(csv_db "${ARGUS_TEST_DIR}/catalog-csv.db")
set(json_db "${ARGUS_TEST_DIR}/catalog-json.db")
file(REMOVE "${csv_db}" "${json_db}")

function(inventory expected_status)
    execute_process(
        COMMAND "${ARGUS_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE
        TIMEOUT 10
    )
    if(NOT status STREQUAL expected_status)
        message(FATAL_ERROR "Unexpected inventory exit '${status}': ${error}")
    endif()
    set(report "${output}" PARENT_SCOPE)
    set(diagnostic "${error}" PARENT_SCOPE)
endfunction()

inventory(0 "${csv_db}" import "${ARGUS_CATALOG_DIR}" fictional-catalog.csv)
string(JSON imported GET "${report}" products)
if(NOT imported EQUAL 4)
    message(FATAL_ERROR "Fixture did not load all four departments")
endif()

inventory(0 "${csv_db}" find 990-01-0001)
string(JSON count LENGTH "${report}" units)
string(JSON first GET "${report}" units 0 epc)
string(JSON category GET "${report}" category)
if(NOT count EQUAL 2 OR NOT first STREQUAL "A00000000000000000000001" OR
   NOT category STREQUAL "clothing")
    message(FATAL_ERROR "Inventory search failed after restart: ${report}")
endif()

inventory(0 "${csv_db}" find 990-01-0001 "${first}")
string(JSON count LENGTH "${report}" units)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "EPC cursor did not advance: ${report}")
endif()

inventory(0 "${json_db}" import "${ARGUS_CATALOG_DIR}" fictional-catalog.json)
inventory(0 "${json_db}" find 990-04-0001)
string(JSON category GET "${report}" category)
if(NOT category STREQUAL "home-goods")
    message(FATAL_ERROR "JSON import did not survive restart: ${report}")
endif()

inventory(1 "${csv_db}" import "${ARGUS_CATALOG_DIR}" fictional-catalog.csv)
if(NOT diagnostic STREQUAL "argus-inventory: duplicate")
    message(FATAL_ERROR "Duplicate diagnostic was ${diagnostic}")
endif()

inventory(1 "${csv_db}" import "${ARGUS_CATALOG_DIR}" ../fictional-catalog.csv)
if(NOT diagnostic STREQUAL "argus-inventory: invalid_import_path")
    message(FATAL_ERROR "Path escape diagnostic was ${diagnostic}")
endif()

inventory(2 "${csv_db}" find "990-01-0001 OR 1=1")
if(NOT diagnostic STREQUAL "argus-inventory: invalid_data")
    message(FATAL_ERROR "Bad identifier diagnostic was ${diagnostic}")
endif()
