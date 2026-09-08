add_library(orcabench_core STATIC
    core/Platform.cpp
    )

target_include_directories(orcabench_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    # GetProcessMemoryInfo
    target_link_libraries(orcabench_core PRIVATE Psapi.lib)
endif ()

set_property(TARGET orcabench_core PROPERTY FOLDER "benchmarks")
