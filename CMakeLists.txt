cmake_minimum_required(VERSION 3.21)

project(norelecbot VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_SOURCE_DIR}/bin")
endforeach()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

# Sets <prefix>_TARGET from pkg-config, or from the header and library names (Windows, vcpkg).
function(norelecbot_find_library prefix package header)
    if(PkgConfig_FOUND)
        pkg_check_modules(${prefix} QUIET IMPORTED_TARGET ${package})
    endif()
    if(TARGET PkgConfig::${prefix})
        set(${prefix}_TARGET PkgConfig::${prefix} PARENT_SCOPE)
        return()
    endif()
    string(TOLOWER "${prefix}_external" target)
    find_path(${prefix}_INCLUDE_DIR NAMES ${header} REQUIRED)
    find_library(${prefix}_LIBRARY NAMES ${ARGN} REQUIRED)
    add_library(${target} UNKNOWN IMPORTED)
    set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION "${${prefix}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${${prefix}_INCLUDE_DIR}"
    )
    set(${prefix}_TARGET ${target} PARENT_SCOPE)
endfunction()

norelecbot_find_library(JANSSON jansson jansson.h jansson)
norelecbot_find_library(MICROHTTPD libmicrohttpd microhttpd.h microhttpd libmicrohttpd)

option(NORELECBOT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NORELECBOT_FUZZ "Build the libFuzzer targets (requires clang)" OFF)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wstrict-prototypes
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
    src/arena.c
    src/dynamic_string.c
    src/platform.c
    src/logging.c
    src/config.c
    src/json_storage.c
    src/conquister_service.c
    src/quote_service.c
    src/telegram_commands.c
    src/rest_routes.c
    src/text.c
)

add_library(norelecbot_core STATIC ${CORE_SOURCES})
target_include_directories(norelecbot_core PUBLIC src)
target_link_libraries(norelecbot_core PUBLIC ${JANSSON_TARGET} Threads::Threads)

add_executable(norelecbot
    src/main.c
    src/telegram.c
    src/http_server.c
)
target_link_libraries(norelecbot PRIVATE
    norelecbot_core
    CURL::libcurl
    ${MICROHTTPD_TARGET}
)

if(WIN32)
    target_link_libraries(norelecbot PRIVATE ws2_32)
endif()

if(NORELECBOT_FUZZ)
    add_executable(fuzz-rest-routes fuzz/fuzz_rest_routes.c)
    target_link_libraries(fuzz-rest-routes PRIVATE norelecbot_core)
    target_link_options(fuzz-rest-routes PRIVATE -fsanitize=fuzzer)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(test arena dynamic-string platform config services telegram-commands rest-routes text)
        string(REPLACE "-" "_" source "${test}")
        add_executable(test-${test} tests/test_${source}.c)
        target_link_libraries(test-${test} PRIVATE norelecbot_core)
        add_test(NAME ${test} COMMAND test-${test})
    endforeach()
endif()
