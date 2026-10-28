cmake_minimum_required(VERSION 3.24)
project(rx_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(RX_ANTLR_VERSION 4.13.2)
set(rx_runtime_archive
    "${PROJECT_SOURCE_DIR}/target/deps/antlr4-cpp-runtime-${RX_ANTLR_VERSION}-source.zip")
if(EXISTS "${rx_runtime_archive}")
    set(rx_runtime_url "${rx_runtime_archive}")
else()
    set(rx_runtime_url
        "https://www.antlr.org/download/antlr4-cpp-runtime-${RX_ANTLR_VERSION}-source.zip")
endif()
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "Build ANTLR's own tests" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "Build shared ANTLR runtime" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "Build static ANTLR runtime" FORCE)
set(WITH_DEMO OFF CACHE BOOL "Build ANTLR demo" FORCE)
set(DISABLE_WARNINGS ON CACHE BOOL "Suppress third-party ANTLR warnings" FORCE)
FetchContent_Declare(antlr4_runtime
    URL "${rx_runtime_url}"
    URL_HASH SHA256=0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(antlr4_runtime)

add_executable(rxc
    src/main.cpp
    src/ast/astbuilder.cpp
    src/ast/astdump.cpp
    src/semantic/checker.cpp
    src/semantic/model.cpp
    generated/Lexer.cpp
    generated/Parser.cpp)
target_include_directories(rxc PRIVATE src generated)
target_include_directories(rxc SYSTEM PRIVATE "${antlr4_runtime_SOURCE_DIR}/runtime/src")
target_link_libraries(rxc PRIVATE antlr4_static)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rxc PRIVATE -Wall -Wextra -Wpedantic)
endif()
