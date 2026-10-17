cmake_minimum_required(VERSION 3.22)
project(gxr_spacewarp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT EXISTS "${OPENXR_SDK_SOURCE_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "Supply OPENXR_SDK_SOURCE_DIR")
endif()
# Android build (mirror codecs-support-layer). The daily build path is
# Build-Native.ps1; this mirrors the other extensions for cmake-driven flows.
# The sibling builder's estimator files are added when present (spacewarp_est.cpp
# + swp_est_worker.cpp); without them swp_dispatch.cpp builds with its fail-safe
# stand-down stubs (guard: __has_include("spacewarp_est.h")).
set(SWP_SOURCES
    src/swp_dispatch.cpp
    src/swp_hooks.cpp
    src/swp_egl.cpp
    src/swp_vts.cpp
    src/xr_layer_entry.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/spacewarp_est.cpp")
    list(APPEND SWP_SOURCES src/spacewarp_est.cpp)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/swp_est_worker.cpp")
    list(APPEND SWP_SOURCES src/swp_est_worker.cpp)
endif()
add_library(gxr_spacewarp SHARED ${SWP_SOURCES})
target_include_directories(gxr_spacewarp PRIVATE "${OPENXR_SDK_SOURCE_DIR}/include")
target_compile_definitions(gxr_spacewarp PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES)
target_compile_options(gxr_spacewarp PRIVATE -O2 -Wall -Wextra -Werror
    -Wno-missing-field-initializers -ffunction-sections -fdata-sections)
target_link_options(gxr_spacewarp PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384
    -Wl,--exclude-libs,ALL)
target_link_libraries(gxr_spacewarp PRIVATE log EGL GLESv3)
set_target_properties(gxr_spacewarp PROPERTIES CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)