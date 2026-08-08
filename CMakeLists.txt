cmake_minimum_required(VERSION 3.22)

# ------------------------------------------------------------------------------
# Pre-declare Googletest BEFORE dependencies (mnemosyne) are included.
# Fixes the 'fatal: unable to read tree' error by using a clean release tag.
# ------------------------------------------------------------------------------
include(FetchContent)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CPM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

project(Necromancer)

option(NECROMANCER_AVX2 "Build with AVX2 codegen" OFF)

set(NECROMANCER_BUILD_TIMESTAMP "" CACHE STRING
        "Optional fixed Necromancer build timestamp in YYYY-MM-DD_HH-mm-ssZ UTC format")
if (NECROMANCER_BUILD_TIMESTAMP AND NOT NECROMANCER_BUILD_TIMESTAMP MATCHES
        "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]_[0-9][0-9]-[0-9][0-9]-[0-9][0-9]Z$")
    message(FATAL_ERROR
            "NECROMANCER_BUILD_TIMESTAMP must use YYYY-MM-DD_HH-mm-ssZ UTC+0 format; got '${NECROMANCER_BUILD_TIMESTAMP}'")
endif ()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel;Nightly" CACHE STRING "" FORCE)

string(APPEND CMAKE_CXX_FLAGS " /wd4100")

# Nightly keeps RelWithDebInfo-style compilation so we get usable symbols, but
# links like a shipped binary so the DLL doesn't become a My 600-Lb. Life contestant
set(CMAKE_C_FLAGS_NIGHTLY "${CMAKE_C_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_NIGHTLY "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_NIGHTLY "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_NIGHTLY "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

string(REPLACE "/Ob2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/Ob1" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REPLACE "/Ob1" "" CMAKE_CXX_FLAGS_NIGHTLY "${CMAKE_CXX_FLAGS_NIGHTLY}")

if (NOT MSVC)
    message(FATAL_ERROR "Compiler unsupported, must use MSVC")
endif()

# Debug info format: Release is a clean optimized binary (no PDB), Nightly
# keeps a full PDB for crash diagnosis.
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:EditAndContinue>$<$<CONFIG:Nightly>:ProgramDatabase>")
endif ()

# MSVC runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Add all sources in src/* and deps/*
file(GLOB_RECURSE Necromancer_SRC CONFIGURE_DEPENDS
        "src/*.h"
        "src/*.cpp"
        "deps/*.h"
        "deps/*.cpp"
)

# Assets
function(ADD_RESOURCES out_var)
    set(result)
    foreach(in_f ${ARGN})
        file(RELATIVE_PATH src_f ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${in_f})
        set(out_f "${PROJECT_BINARY_DIR}/${in_f}.o")

        add_custom_command(
                OUTPUT ${out_f}
                COMMAND ld.exe -r -b binary -o ${out_f} ${src_f}
                DEPENDS ${in_f}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Building object ${out_f}"
                VERBATIM
        )

        list(APPEND result ${out_f})
    endforeach()

    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# Add all files in assets/ as resources
file(GLOB_RECURSE RESOURCE_FILES
        RELATIVE ${CMAKE_CURRENT_LIST_DIR}
        CONFIGURE_DEPENDS
        "assets/*"
)

add_resources(EMBEDDED_RESOURCES ${RESOURCE_FILES})

set(NECROMANCER_BUILD_TIMESTAMP_SOURCE
        "${PROJECT_BINARY_DIR}/generated/NecromancerBuildTimestamp.cpp")
set_source_files_properties("${NECROMANCER_BUILD_TIMESTAMP_SOURCE}" PROPERTIES GENERATED TRUE)

add_custom_target(NecromancerBuildTimestampGenerator
        COMMAND ${CMAKE_COMMAND}
        "-DNECROMANCER_BUILD_TIMESTAMP=${NECROMANCER_BUILD_TIMESTAMP}"
        "-DOUTPUT_FILE=${NECROMANCER_BUILD_TIMESTAMP_SOURCE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildTimestamp.cmake"
        BYPRODUCTS "${NECROMANCER_BUILD_TIMESTAMP_SOURCE}"
        COMMENT "Refreshing Necromancer UTC build timestamp"
        VERBATIM
)

add_library(Necromancer SHARED
        ${Necromancer_SRC}
        ${EMBEDDED_RESOURCES}
        "${NECROMANCER_BUILD_TIMESTAMP_SOURCE}"
)
add_dependencies(Necromancer NecromancerBuildTimestampGenerator)

if (NECROMANCER_AVX2)
    set(NECROMANCER_NAME_SUFFIX "AVX2")
else ()
    set(NECROMANCER_NAME_SUFFIX "")
endif ()

set_target_properties(Necromancer PROPERTIES
        OUTPUT_NAME_RELEASE "Necromancer${NECROMANCER_NAME_SUFFIX}"
        PDB_NAME_RELEASE "Necromancer${NECROMANCER_NAME_SUFFIX}"
        OUTPUT_NAME_RELWITHDEBINFO "Necromancer${NECROMANCER_NAME_SUFFIX}"
        PDB_NAME_RELWITHDEBINFO "Necromancer${NECROMANCER_NAME_SUFFIX}"
        OUTPUT_NAME_DEBUG "NecromancerDebug${NECROMANCER_NAME_SUFFIX}"
        PDB_NAME_DEBUG "NecromancerDebug${NECROMANCER_NAME_SUFFIX}"
        OUTPUT_NAME_NIGHTLY "NecromancerNightly${NECROMANCER_NAME_SUFFIX}"
        PDB_NAME_NIGHTLY "NecromancerNightly${NECROMANCER_NAME_SUFFIX}"
)

# C++ standard
set_property(TARGET Necromancer PROPERTY CXX_STANDARD 23)
set_property(TARGET Necromancer PROPERTY CXX_STANDARD_REQUIRED ON)

# Compile definitions
target_compile_definitions(Necromancer PRIVATE
        _UNICODE
        UNICODE
        _HAS_STATIC_RTTI=0

        $<$<CONFIG:Debug>:NECROMANCER_DEBUG>
        $<$<CONFIG:Debug>:NECROMANCER_CRASH_REPORTING>
        $<$<CONFIG:Release>:NECROMANCER_RELEASE>
        $<$<CONFIG:Release>:NECROMANCER_CRASH_REPORTING>
        $<$<CONFIG:RelWithDebInfo>:NECROMANCER_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:NECROMANCER_CRASH_REPORTING>
        $<$<CONFIG:MinSizeRel>:NECROMANCER_RELEASE>
        $<$<CONFIG:Nightly>:NECROMANCER_NIGHTLY>
        $<$<CONFIG:Nightly>:NECROMANCER_CRASH_REPORTING>
)

target_compile_options(Necromancer PRIVATE
        /EHsc
        /GR-
        /W4
        /wd4100
        /fp:precise
        $<$<NOT:$<CONFIG:Debug>>:/O2 /Ob3 /Oi /Ot /Gy /Gw /GL /Zc:inline>
        $<$<BOOL:${NECROMANCER_AVX2}>:/arch:AVX2>
)

target_link_options(Necromancer PRIVATE
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/LTCG>

        $<$<CONFIG:Nightly>:/DEBUG:FULL>
        $<$<CONFIG:Nightly>:/INCREMENTAL:NO>
        $<$<CONFIG:Nightly>:/OPT:REF>
        $<$<CONFIG:Nightly>:/OPT:NOICF>
        $<$<CONFIG:Nightly>:/LTCG>
)

target_include_directories(Necromancer PRIVATE
        src
        deps
)

# Link libraries
target_link_libraries(Necromancer PRIVATE
        # WinRT
        WindowsApp.lib
        Shlwapi.lib

        # Needed for getting app version
        Version.lib

        # Needed for DirectX hooking and Direct2D rendering
        d3d11
        d3d12
        dxguid
        dxgi

        # UI rendering
        d2d1

        # Text rendering
        dwrite

        # ImageNtHeader for mnemosyne
        Dbghelp

        # PlaySound for scripting
        winmm
)

add_subdirectory(src)
add_subdirectory(deps)