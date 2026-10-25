cmake_minimum_required(VERSION 3.16)
project(gxr_quad_layer_host_tests LANGUAGES CXX)

enable_testing()

set(OPENXR_SDK_SOURCE_DIR "" CACHE PATH "OpenXR SDK checkout with include/openxr headers")
if(OPENXR_SDK_SOURCE_DIR)
    foreach(quad_only IN ITEMS 0 1)
        if(quad_only)
            set(test_target quad_layer_tests_only)
            set(test_name quad_layer_tests_only)
        else()
            set(test_target quad_layer_tests_fovea)
            set(test_name quad_layer_tests_fovea)
        endif()
        add_executable(${test_target} quad_layer_tests.cpp)
        target_compile_features(${test_target} PRIVATE cxx_std_17)
        target_include_directories(${test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../resolution-trace-layer/tests/stubs"
            "${OPENXR_SDK_SOURCE_DIR}/include")
        target_compile_definitions(${test_target} PRIVATE
            GXR_QUAD_ONLY=${quad_only})
        add_test(NAME ${test_name} COMMAND ${test_target})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endforeach()
endif()
