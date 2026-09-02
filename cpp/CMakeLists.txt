cmake_minimum_required(VERSION 3.16)
project(dealcode_cpp VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PROJECT_IS_TOP_LEVEL needs CMake 3.21; compute it for 3.16 compatibility.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(DEALCODE_TOP_LEVEL ON)
else()
    set(DEALCODE_TOP_LEVEL OFF)
endif()

option(DEALCODE_BUILD_TESTS
    "Build the dealcode test suite (needs python3 for vector codegen)"
    ${DEALCODE_TOP_LEVEL})
option(DEALCODE_WERROR "Treat warnings as errors" ${DEALCODE_TOP_LEVEL})

find_package(OpenSSL REQUIRED)

set(DEALCODE_C_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../c)

set(DEALCODE_WARNINGS -Wall -Wextra)
if(DEALCODE_WERROR)
    list(APPEND DEALCODE_WARNINGS -Werror)
endif()

# --- C core (the algorithm lives here; not duplicated in C++) -------------
add_library(dealcode_core STATIC ${DEALCODE_C_DIR}/src/dealcode.c)
target_include_directories(dealcode_core PUBLIC
    $<BUILD_INTERFACE:${DEALCODE_C_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(dealcode_core PUBLIC OpenSSL::Crypto)
target_compile_options(dealcode_core PRIVATE ${DEALCODE_WARNINGS})

# --- C++ wrapper (header-only) ---------------------------------------------
add_library(dealcode_cpp INTERFACE)
target_include_directories(dealcode_cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(dealcode_cpp INTERFACE dealcode_core)

# Consumers (add_subdirectory/FetchContent and find_package alike) link the
# same name: dealcode::dealcode.
set_target_properties(dealcode_cpp PROPERTIES EXPORT_NAME dealcode)
add_library(dealcode::dealcode ALIAS dealcode_cpp)

# --- install / export -------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS dealcode_core dealcode_cpp
    EXPORT dealcode-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
    ${DEALCODE_C_DIR}/include/dealcode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/dealcode.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT dealcode-targets
    NAMESPACE dealcode::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dealcode)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dealcode-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dealcode-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dealcode)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/dealcode-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dealcode-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dealcode-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dealcode)

# --- tests (gated: consumers never need Python3) ----------------------------
if(DEALCODE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    enable_testing()

    set(VECTORS_INC ${CMAKE_CURRENT_BINARY_DIR}/vectors.inc)
    add_custom_command(
        OUTPUT ${VECTORS_INC}
        COMMAND ${Python3_EXECUTABLE} ${DEALCODE_C_DIR}/tests/gen_vectors.py
                ${VECTORS_INC}
        DEPENDS ${DEALCODE_C_DIR}/tests/gen_vectors.py
                ${CMAKE_CURRENT_SOURCE_DIR}/../testvectors/ff1_nist.json
                ${CMAKE_CURRENT_SOURCE_DIR}/../testvectors/v1.json
                ${CMAKE_CURRENT_SOURCE_DIR}/../testvectors/v1c.json
                ${CMAKE_CURRENT_SOURCE_DIR}/../testvectors/v1r.json
        COMMENT "Generating test vectors")
    add_custom_target(dealcode_vectors DEPENDS ${VECTORS_INC})

    add_executable(test_dealcode_cpp tests/test_dealcode.cpp)
    add_dependencies(test_dealcode_cpp dealcode_vectors)
    target_include_directories(test_dealcode_cpp PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}   # vectors.inc
        ${DEALCODE_C_DIR}/src)        # private FF1 seam for NIST vectors
    target_link_libraries(test_dealcode_cpp PRIVATE dealcode::dealcode)
    target_compile_options(test_dealcode_cpp PRIVATE ${DEALCODE_WARNINGS})

    add_test(NAME dealcode_cpp COMMAND test_dealcode_cpp)
endif()
