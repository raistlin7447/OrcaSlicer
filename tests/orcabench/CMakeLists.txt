get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    test_buildid.cpp
    test_document.cpp
    test_host.cpp
    test_measurement.cpp
    test_options.cpp
    test_policy.cpp
    test_result.cpp
    test_runner.cpp
    test_sampler.cpp
    test_summary.cpp
    test_workload.cpp
    )

target_link_libraries(${_TEST_NAME}_tests test_common orcabench_core Catch2::Catch2WithMain)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# A build with neither a git checkout nor the git_commit_hash variable, such as a source tarball,
# records GitCommitHash.cmake's placeholder, and the revision test skips there.
find_package(Git QUIET)
set(_has_commit 0)
if ((GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git") OR NOT "$ENV{git_commit_hash}" STREQUAL "")
    set(_has_commit 1)
endif ()
target_compile_definitions(${_TEST_NAME}_tests PRIVATE ORCABENCH_HAS_COMMIT=${_has_commit})

orcaslicer_discover_tests(${_TEST_NAME}_tests)

# orca_bench builds outside build/tests, which is all the CI unit-test job receives, and
# run_orca_bench.cmake checks its exit code as well as its output.
set(_run_orca_bench "${CMAKE_CURRENT_SOURCE_DIR}/run_orca_bench.cmake")
add_test(NAME orca_bench_lists_the_catalog
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:orca_bench> -DARGS=--list -DEXIT=0 -P ${_run_orca_bench})
add_test(NAME orca_bench_refuses_an_unknown_argument
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:orca_bench> -DARGS=--bogus -DEXIT=2
                 "-DOUTPUT=unknown argument '--bogus'" -P ${_run_orca_bench})
add_test(NAME orca_bench_prints_the_usage_for_help_beside_list
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:orca_bench> -DARGS=--list$<SEMICOLON>--help -DEXIT=0
                 "-DOUTPUT=usage: orca_bench" -P ${_run_orca_bench})
set_tests_properties(orca_bench_lists_the_catalog orca_bench_refuses_an_unknown_argument
                     orca_bench_prints_the_usage_for_help_beside_list PROPERTIES LABELS RequiresApp)

# GitCommitHash.cmake lives outside build/tests too, and git may be missing where tests run.
add_test(NAME git_commit_hash_counts_an_untracked_kind
         COMMAND ${CMAKE_COMMAND} -DSCRIPT=${CMAKE_SOURCE_DIR}/cmake/modules/GitCommitHash.cmake
                 -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/git_commit_hash_probe
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/test_git_commit_hash.cmake)
set_tests_properties(git_commit_hash_counts_an_untracked_kind PROPERTIES LABELS RequiresApp
                     SKIP_REGULAR_EXPRESSION "SKIP: ")
