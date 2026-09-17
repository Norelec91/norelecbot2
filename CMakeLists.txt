cmake_minimum_required(VERSION 3.21)

project(norelecbot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_SOURCE_DIR}/bin")
endforeach()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(spdlog REQUIRED)
# vcpkg ships a CMake package for cpp-httplib, Debian only a pkg-config file.
find_package(httplib CONFIG QUIET)
if(NOT TARGET httplib::httplib)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
    add_library(httplib::httplib ALIAS PkgConfig::HTTPLIB)
endif()

option(NORELECBOT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NORELECBOT_FUZZ "Build the libFuzzer targets (requires clang)" OFF)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /WX /utf-8 /permissive- /EHsc)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Werror
    )
    if(NORELECBOT_SANITIZE OR NORELECBOT_FUZZ)
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
    if(NORELECBOT_FUZZ)
        add_compile_options(-fsanitize=fuzzer-no-link)
    endif()
endif()

set(CORE_SOURCES
    src/logging.cpp
    src/config.cpp
    src/storage.cpp
    src/game.cpp
    src/commands.cpp
    src/irc_protocol.cpp
    src/irc_session.cpp
    src/rest_routes.cpp
    src/text.cpp
    src/zodiac.cpp
    src/position.cpp
)

add_library(norelecbot_core STATIC ${CORE_SOURCES})
target_include_directories(norelecbot_core PUBLIC src)
target_link_libraries(norelecbot_core PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog Threads::Threads)

add_executable(norelecbot
    src/main.cpp
    src/telegram.cpp
    src/irc.cpp
    src/raids.cpp
    src/http_server.cpp
)
target_link_libraries(norelecbot PRIVATE norelecbot_core httplib::httplib OpenSSL::SSL OpenSSL::Crypto)

if(NORELECBOT_FUZZ)
    add_executable(fuzz-rest-routes fuzz/fuzz_rest_routes.cpp)
    target_link_libraries(fuzz-rest-routes PRIVATE norelecbot_core)
    target_link_options(fuzz-rest-routes PRIVATE -fsanitize=fuzzer)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(doctest REQUIRED)
    foreach(test config services commands rest-routes text zodiac position irc-protocol irc-session)
        string(REPLACE "-" "_" source "${test}")
        add_executable(test-${test} tests/test_${source}.cpp)
        target_link_libraries(test-${test} PRIVATE norelecbot_core doctest::doctest)
        add_test(NAME ${test} COMMAND test-${test})
    endforeach()
endif()
