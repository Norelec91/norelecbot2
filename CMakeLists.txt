cmake_minimum_required(VERSION 3.20)

project(norelecbot VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
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

if(PkgConfig_FOUND)
    pkg_check_modules(JANSSON QUIET IMPORTED_TARGET jansson)
    pkg_check_modules(MICROHTTPD QUIET IMPORTED_TARGET libmicrohttpd)
endif()

if(TARGET PkgConfig::JANSSON)
    set(JANSSON_TARGET PkgConfig::JANSSON)
else()
    find_path(JANSSON_INCLUDE_DIR NAMES jansson.h REQUIRED)
    find_library(JANSSON_LIBRARY NAMES jansson REQUIRED)
    add_library(jansson_external UNKNOWN IMPORTED)
    set_target_properties(jansson_external PROPERTIES
        IMPORTED_LOCATION "${JANSSON_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${JANSSON_INCLUDE_DIR}"
    )
    set(JANSSON_TARGET jansson_external)
endif()

if(TARGET PkgConfig::MICROHTTPD)
    set(MICROHTTPD_TARGET PkgConfig::MICROHTTPD)
else()
    find_path(MICROHTTPD_INCLUDE_DIR NAMES microhttpd.h REQUIRED)
    find_library(MICROHTTPD_LIBRARY NAMES microhttpd libmicrohttpd REQUIRED)
    add_library(microhttpd_external UNKNOWN IMPORTED)
    set_target_properties(microhttpd_external PROPERTIES
        IMPORTED_LOCATION "${MICROHTTPD_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MICROHTTPD_INCLUDE_DIR}"
    )
    set(MICROHTTPD_TARGET microhttpd_external)
endif()

option(NORELECBOT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

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
    if(NORELECBOT_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

set(CORE_SOURCES
    src/dynamic_string.c
    src/platform.c
    src/logging.c
    src/config.c
    src/json_storage.c
    src/conquister_service.c
    src/quote_service.c
    src/telegram_commands.c
    src/rest_routes.c
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

include(CTest)
if(BUILD_TESTING)
    add_executable(test-dynamic-string tests/test_dynamic_string.c)
    target_link_libraries(test-dynamic-string PRIVATE norelecbot_core)

    add_executable(test-platform tests/test_platform.c)
    target_link_libraries(test-platform PRIVATE norelecbot_core)

    add_executable(test-config tests/test_config.c)
    target_link_libraries(test-config PRIVATE norelecbot_core)

    add_executable(test-services tests/test_services.c)
    target_link_libraries(test-services PRIVATE norelecbot_core)

    add_executable(test-telegram-commands tests/test_telegram_commands.c)
    target_link_libraries(test-telegram-commands PRIVATE norelecbot_core)

    add_executable(test-rest-routes tests/test_rest_routes.c)
    target_link_libraries(test-rest-routes PRIVATE norelecbot_core)

    add_test(NAME dynamic-string COMMAND test-dynamic-string)
    add_test(NAME platform COMMAND test-platform)
    add_test(NAME config COMMAND test-config)
    add_test(NAME services COMMAND test-services)
    add_test(NAME telegram-commands COMMAND test-telegram-commands)
    add_test(NAME rest-routes COMMAND test-rest-routes)
endif()
