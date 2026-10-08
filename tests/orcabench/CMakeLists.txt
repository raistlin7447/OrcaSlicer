get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    test_buildid.cpp
    test_document.cpp
    test_host.cpp
    test_result.cpp
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
