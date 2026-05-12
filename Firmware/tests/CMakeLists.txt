# Test runner umbrella — registers every individual ztest case (not just
# module-level entries) with CTest, so the VSCode Test Explorer can
# discover, run, and debug them one-at-a-time.
#
# Strategy: for each test directory under tests/ that has a CMakeLists.txt,
#   1. (re)build it via scripts/native_test.py (incremental, ~0s if cached)
#   2. invoke the built binary with ztest's `-list` flag to enumerate
#      every "suite::test" case
#   3. register a CTest entry per case that runs the binary with
#      `-test=<suite>::<case>` so the framework filters to just that one
#
# Cold-configure cost: roughly one full sweep build (~2 min for ten test
# targets on a 12-core box). Warm reconfigure is essentially free —
# `-list` is a sub-second operation per binary.
#
# This is *not* a Zephyr application and does not get built into firmware.
# It is configured separately and CMake Tools is pointed at this umbrella
# in `.vscode/settings.json`:
#
#   cmake -S Firmware/tests -B Firmware/build-native/_runner
#
# Add a new ztest case → just rebuild and reconfigure. The case appears
# in the Testing tab automatically.

cmake_minimum_required(VERSION 3.20)
project(divecan_native_tests LANGUAGES NONE)

include(CTest)
enable_testing()

set(NATIVE_TEST_RUNNER
    "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_test.py"
    CACHE FILEPATH "Wrapper script that builds a native_sim test"
)
set(BUILD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../build-native")

# Discover every test directory that has its own CMakeLists.txt — new
# tests get picked up automatically with no manual bookkeeping.
file(GLOB _candidates RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach(_test_dir IN LISTS _candidates)
    set(_src_path ${CMAKE_CURRENT_SOURCE_DIR}/${_test_dir})
    if(NOT IS_DIRECTORY ${_src_path})
        continue()
    endif()
    if(NOT EXISTS ${_src_path}/CMakeLists.txt)
        continue()
    endif()

    set(_binary ${BUILD_ROOT}/${_test_dir}/zephyr/zephyr.exe)

    # Step 1: ensure the binary exists. Incremental — runs west build,
    # which is a no-op if everything is up-to-date.
    message(STATUS "Native test umbrella: preparing ${_test_dir}")
    execute_process(
        COMMAND ${NATIVE_TEST_RUNNER} build ${_test_dir}
        OUTPUT_QUIET
        ERROR_VARIABLE _build_err
        RESULT_VARIABLE _build_rc
    )

    if(NOT _build_rc EQUAL 0)
        message(WARNING
            "Native test umbrella: build of '${_test_dir}' failed (rc=${_build_rc}); "
            "registering a single failing CTest entry so the Test Explorer surfaces it. "
            "Stderr:\n${_build_err}"
        )
        add_test(NAME ${_test_dir}::BUILD_FAILED
                 COMMAND ${CMAKE_COMMAND} -E false)
        continue()
    endif()

    if(NOT EXISTS ${_binary})
        message(WARNING
            "Native test umbrella: binary ${_binary} missing after build "
            "of '${_test_dir}'; skipping case discovery."
        )
        continue()
    endif()

    # Step 2: enumerate "suite::case" lines from ztest's -list output.
    execute_process(
        COMMAND ${_binary} -list
        OUTPUT_VARIABLE _list_out
        ERROR_QUIET
        RESULT_VARIABLE _list_rc
    )

    # Match identifiers separated by "::" — robust to the boot banner and
    # trailing TESTSUITE SUMMARY noise that ztest prints around the listing.
    string(REGEX MATCHALL "[A-Za-z0-9_]+::[A-Za-z0-9_]+" _cases "${_list_out}")
    list(REMOVE_DUPLICATES _cases)
    list(LENGTH _cases _case_count)

    if(_case_count EQUAL 0)
        message(WARNING
            "Native test umbrella: no cases discovered for '${_test_dir}'. "
            "Falling back to a module-level entry. ztest -list output:\n${_list_out}"
        )
        add_test(NAME ${_test_dir}
                 COMMAND ${_binary})
        continue()
    endif()

    message(STATUS "  ${_test_dir}: ${_case_count} cases")

    # Step 3a: build a `suite::case → file:line` map by scanning the C
    # sources. CMake Tools' Test Explorer reads the DEF_SOURCE_LINE
    # property to wire up "Go to Test" navigation, so each registered
    # CTest entry needs that property pointing at its ZTEST(...) macro.
    execute_process(
        COMMAND ${NATIVE_TEST_RUNNER} discover-cases ${_test_dir}
        OUTPUT_VARIABLE _locations_out
        ERROR_QUIET
        RESULT_VARIABLE _loc_rc
    )

    set(_loc_keys "")
    set(_loc_vals "")
    if(_loc_rc EQUAL 0)
        # Each line: suite::case|/abs/path:line — split into parallel lists.
        string(REPLACE "\n" ";" _loc_lines "${_locations_out}")
        foreach(_line IN LISTS _loc_lines)
            if(_line MATCHES "^([^|]+)\\|(.+)$")
                list(APPEND _loc_keys "${CMAKE_MATCH_1}")
                list(APPEND _loc_vals "${CMAKE_MATCH_2}")
            endif()
        endforeach()
    endif()

    # Step 3b: one CTest entry per individual case. Forward slashes
    # structure the name as <module>/<suite>/<case> so CMake Tools'
    # Test Explorer renders them as a tree (cmake.ctest.testSuiteDelimiter
    # is set to "/" in .vscode/settings.json).
    foreach(_case IN LISTS _cases)
        # _case looks like "pid_update_suite::test_step_response"
        string(REPLACE "::" "/" _case_path "${_case}")
        set(_ctest_name "${_test_dir}/${_case_path}")

        add_test(
            NAME ${_ctest_name}
            COMMAND ${_binary} -test=${_case}
        )

        set(_test_props
            LABELS "${_test_dir}"
            FAIL_REGULAR_EXPRESSION "FAIL|PROJECT EXECUTION FAILED"
        )

        # Match this case against the discovered location map.
        list(FIND _loc_keys "${_case}" _idx)
        if(NOT _idx EQUAL -1)
            list(GET _loc_vals ${_idx} _location)
            list(APPEND _test_props DEF_SOURCE_LINE "${_location}")
        endif()

        set_tests_properties(${_ctest_name} PROPERTIES ${_test_props})
    endforeach()
endforeach()
