get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    test_platform.cpp
    )

target_link_libraries(${_TEST_NAME}_tests test_common orcabench_core Catch2::Catch2WithMain)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

orcaslicer_discover_tests(${_TEST_NAME}_tests)
