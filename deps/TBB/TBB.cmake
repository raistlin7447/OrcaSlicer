orcaslicer_add_cmake_project(
    TBB
    URL "https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v2023.2.0.zip"
    URL_HASH SHA256=6a7cf5f1873afd6efb82af3de40ae08f4898cd4fe7de5d7bf96cd24a6546cc9b
    CMAKE_ARGS
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        -DTBB_TEST=OFF
        -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON
        -DTBB_ENABLE_IPO=OFF
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)

if (MSVC)
    add_debug_dep(dep_TBB)
endif ()


