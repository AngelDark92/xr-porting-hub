cmake_minimum_required(VERSION 3.22)
project(gxr_quad_layer LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Native helper build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENXR_SDK_SOURCE_DIR "" CACHE PATH "Path to an OpenXR-SDK checkout")
if(NOT OPENXR_SDK_SOURCE_DIR)
    set(_bundled_sdk
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../builds/steamlink-patches/extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src")
    if(EXISTS "${_bundled_sdk}/include/openxr/openxr.h")
        set(OPENXR_SDK_SOURCE_DIR "${_bundled_sdk}")
    endif()
endif()
if(NOT EXISTS "${OPENXR_SDK_SOURCE_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "Set OPENXR_SDK_SOURCE_DIR to an OpenXR SDK checkout")
endif()

function(add_gxr_quad_target target output_name quad_only)
    add_library(${target} SHARED
        src/quad_layer.cpp
    )
    target_include_directories(${target} PRIVATE
        "${OPENXR_SDK_SOURCE_DIR}/include"
    )
    target_compile_definitions(${target} PRIVATE
        NDEBUG
        GXR_QUAD_ONLY=${quad_only}
    )
    target_compile_options(${target} PRIVATE
        -O2
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(${target} PRIVATE
        -Wl,--gc-sections
    )
    target_link_libraries(${target} PRIVATE log)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME ${output_name}
    )
    if(CMAKE_STRIP)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${target}>"
            COMMENT "Strip unneeded symbols from ${output_name}"
            VERBATIM
        )
    endif()
endfunction()

add_gxr_quad_target(
    gxr_fovea_quad
    gxr_fovea_quad
    0
)

add_gxr_quad_target(
    gxr_quad_only
    gxr_quad_only
    1
)
