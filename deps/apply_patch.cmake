# Apply patches to an extracted dependency source, skipping any that are already
# applied. ExternalProject re-runs the patch step whenever the patch command or a
# patch file changes, which would otherwise fail against a source tree that an
# earlier build already patched.
#
# Usage: cmake -DGIT_EXECUTABLE=<git> -P apply_patch.cmake -- [--directory <dir>] <patch>...

set(_args "")
set(_past_separator FALSE)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_i RANGE ${_last})
    if(_past_separator)
        list(APPEND _args "${CMAKE_ARGV${_i}}")
    elseif("${CMAKE_ARGV${_i}}" STREQUAL "--")
        set(_past_separator TRUE)
    endif()
endforeach()

# git apply resolves paths against the repository root when it runs inside one,
# so callers building in-tree pass --directory to redirect it at the source.
set(_directory "")
list(LENGTH _args _argc)
if(_argc GREATER 1)
    list(GET _args 0 _first)
    if("${_first}" STREQUAL "--directory")
        list(GET _args 1 _dir)
        set(_directory --directory "${_dir}")
        list(REMOVE_AT _args 0 1)
    endif()
endif()

if(NOT _args)
    message(FATAL_ERROR "No patch files given")
endif()

# core.autocrlf=false: otherwise git apply rewrites patched files to LF, and
# cmd.exe cannot resolve goto labels in an LF batch file.
set(_git ${GIT_EXECUTABLE} -c core.autocrlf=false apply ${_directory}
         --ignore-space-change --whitespace=fix)

foreach(_patch IN LISTS _args)
    if(NOT EXISTS "${_patch}")
        message(FATAL_ERROR "Patch file not found: ${_patch}")
    endif()

    execute_process(
        COMMAND ${_git} --reverse --check "${_patch}"
        RESULT_VARIABLE _already_applied
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_already_applied EQUAL 0)
        message(STATUS "Already applied, skipping: ${_patch}")
        continue()
    endif()

    execute_process(
        COMMAND ${_git} --verbose "${_patch}"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${_patch}")
    endif()
endforeach()
