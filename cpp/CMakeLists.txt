cmake_minimum_required(VERSION 3.16)
project(dealcode_cpp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(DEALCODE_C_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../c)

# --- C core (the algorithm lives here; not duplicated in C++) -------------
add_library(dealcode_core STATIC ${DEALCODE_C_DIR}/src/dealcode.c)
target_include_directories(dealcode_core PUBLIC ${DEALCODE_C_DIR}/include)
target_link_libraries(dealcode_core PUBLIC OpenSSL::Crypto)
target_compile_options(dealcode_core PRIVATE -Wall -Wextra -Werror)

# --- C++ wrapper (header-only) ---------------------------------------------
add_library(dealcode_cpp INTERFACE)
target_include_directories(dealcode_cpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dealcode_cpp INTERFACE dealcode_core)

# --- tests ------------------------------------------------------------------
enable_testing()

set(VECTORS_INC ${CMAKE_CURRENT_BINARY_DIR}/vectors.inc)
add_custom_command(
    OUTPUT ${VECTORS_INC}
    COMMAND ${Python3_EXECUTABLE} ${DEALCODE_C_DIR}/tests/gen_vectors.py
            ${VECTORS_INC}
    DEPENDS ${DEALCODE_C_DIR}/tests/gen_vectors.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../testvectors/ff1_nist.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../testvectors/v1.json
    COMMENT "Generating test vectors")
add_custom_target(dealcode_vectors DEPENDS ${VECTORS_INC})

add_executable(test_dealcode_cpp tests/test_dealcode.cpp)
add_dependencies(test_dealcode_cpp dealcode_vectors)
target_include_directories(test_dealcode_cpp PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}   # vectors.inc
    ${DEALCODE_C_DIR}/src)        # private FF1 seam for NIST vectors
target_link_libraries(test_dealcode_cpp PRIVATE dealcode_cpp)
target_compile_options(test_dealcode_cpp PRIVATE -Wall -Wextra -Werror)

add_test(NAME dealcode_cpp COMMAND test_dealcode_cpp)
