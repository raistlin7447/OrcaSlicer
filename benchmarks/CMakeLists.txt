add_library(orcabench_core STATIC
    core/BuildId.cpp
    core/Document.cpp
    core/Host.cpp
    core/Policy.cpp
    core/Result.cpp
    )

target_include_directories(orcabench_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# json.hpp includes its siblings as <nlohmann/...>, which the nlohmann_json target's include
# directory cannot resolve.
target_link_libraries(orcabench_core PRIVATE nlohmann_json)
target_include_directories(orcabench_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/deps_src)

target_include_directories(orcabench_core PRIVATE ${ORCA_GIT_COMMIT_HASH_DIR})
add_dependencies(orcabench_core git_commit_hash_header)

# Records the compiler and the flags code here compiles with, including the add_compile_options
# that cache options such as SLIC3R_ASAN turn on, except those written as generator expressions.
get_directory_property(_orcabench_options COMPILE_OPTIONS)
string(GENEX_STRIP "${_orcabench_options}" _orcabench_options)
list(JOIN _orcabench_options " " _orcabench_options)
set(_orcabench_configs ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
list(REMOVE_DUPLICATES _orcabench_configs)
foreach (_config IN LISTS _orcabench_configs)
    string(TOUPPER "${_config}" _config_upper)
    string(STRIP "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_config_upper}} ${_orcabench_options}" _flags)
    string(REPLACE "\\" "\\\\" _flags "${_flags}")
    string(REPLACE "\"" "\\\"" _flags "${_flags}")
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${_config}/orcabench_build.h"
        CONTENT "#pragma once
#define ORCABENCH_COMPILER \"${CMAKE_CXX_COMPILER_ID}\"
#define ORCABENCH_COMPILER_VERSION \"${CMAKE_CXX_COMPILER_VERSION}\"
#define ORCABENCH_FLAGS \"${_flags}\"
#define ORCABENCH_CONFIG \"${_config}\"
")
endforeach ()

target_include_directories(orcabench_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

if (WIN32)
    # GetProcessMemoryInfo
    target_link_libraries(orcabench_core PRIVATE Psapi.lib)
endif ()

set_property(TARGET orcabench_core PROPERTY FOLDER "benchmarks")
