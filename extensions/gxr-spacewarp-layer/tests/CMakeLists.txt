cmake_minimum_required(VERSION 3.16)
project(gxr_spacewarp_host_tests LANGUAGES CXX)

enable_testing()

# The dispatch/arrival logic is exercised end-to-end on the host: production
# TUs (swp_dispatch.cpp, swp_egl.cpp, swp_hooks.cpp) compiled with the sibling
# builder's estimator replaced by test fakes and Android/EGL replaced by the
# stubs. Requires the reused OpenXR headers (the same dir the other layers'
# build trees share): supply OPENXR_SDK_SOURCE_DIR.
set(OPENXR_SDK_SOURCE_DIR "" CACHE PATH "OpenXR SDK checkout with include/openxr headers")
if(NOT OPENXR_SDK_SOURCE_DIR)
    message(FATAL_ERROR "Supply OPENXR_SDK_SOURCE_DIR (reuse extensions/<name>/build-android/_deps/openxr_headers-src)")
endif()
if(NOT EXISTS "${OPENXR_SDK_SOURCE_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "OPENXR_SDK_SOURCE_DIR has no include/openxr/openxr.h")
endif()

add_executable(swp_dispatch_tests swp_dispatch_tests.cpp ../src/swp_dispatch.cpp
                                 ../src/swp_egl.cpp ../src/swp_vts.cpp)
target_compile_features(swp_dispatch_tests PRIVATE cxx_std_17)
target_include_directories(swp_dispatch_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${OPENXR_SDK_SOURCE_DIR}/include")
target_compile_definitions(swp_dispatch_tests PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
    SWP_HOST_TESTS)
add_test(NAME swp_dispatch_tests COMMAND swp_dispatch_tests)
set_tests_properties(swp_dispatch_tests PROPERTIES TIMEOUT 120)

# Brief-10 bridge consumer: the VTS carrier state machine against a MOCKED
# BGetRemoteValue table + fake capture originals (the real scene-lib read is a
# direct pinned call that no host binary can exercise).
add_executable(swp_vts_tests swp_vts_tests.cpp ../src/swp_vts.cpp)
target_compile_features(swp_vts_tests PRIVATE cxx_std_17)
target_include_directories(swp_vts_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_compile_definitions(swp_vts_tests PRIVATE SWP_HOST_TESTS)
add_test(NAME swp_vts_tests COMMAND swp_vts_tests)
set_tests_properties(swp_vts_tests PROPERTIES TIMEOUT 60)

add_executable(swp_hooks_tests swp_hooks_tests.cpp ../src/swp_hooks.cpp ../src/swp_egl.cpp)
target_compile_features(swp_hooks_tests PRIVATE cxx_std_17)
target_include_directories(swp_hooks_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${OPENXR_SDK_SOURCE_DIR}/include")
target_compile_definitions(swp_hooks_tests PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
    SWP_HOST_TESTS)
add_test(NAME swp_hooks_tests COMMAND swp_hooks_tests)
set_tests_properties(swp_hooks_tests PROPERTIES TIMEOUT 60)