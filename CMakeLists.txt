cmake_minimum_required(VERSION 3.25)

project(
    NetMind
    VERSION 0.1.0
    DESCRIPTION "Graph-Based Network Simulation Engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
enable_testing()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(Catch2 nlohmann_json)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CATCH2_JUNIT_SOURCE
        "${catch2_SOURCE_DIR}/src/catch2/reporters/catch_reporter_junit.cpp"
    )
    file(READ "${CATCH2_JUNIT_SOURCE}" CATCH2_JUNIT_CONTENT)
    string(REPLACE
        "#if defined (_MSC_VER) || defined (__MINGW32__)\n            gmtime_s(&timeInfo, &rawtime);"
        "#if defined (_MSC_VER)\n            gmtime_s(&timeInfo, &rawtime);\n#elif defined (__MINGW32__)\n            if (const std::tm* utcTime = std::gmtime(&rawtime)) {\n                timeInfo = *utcTime;\n            }"
        CATCH2_JUNIT_CONTENT
        "${CATCH2_JUNIT_CONTENT}"
    )
    file(WRITE "${CATCH2_JUNIT_SOURCE}" "${CATCH2_JUNIT_CONTENT}")

    add_link_options(-static-libgcc -static-libstdc++)
endif()

file(
    GLOB_RECURSE NETMIND_CORE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

list(
    FILTER NETMIND_CORE_SOURCES
    EXCLUDE REGEX "/src/commands/"
)
list(
    FILTER NETMIND_CORE_SOURCES
    EXCLUDE REGEX "/src/common/Ip\\.cpp$"
)
list(
    FILTER NETMIND_CORE_SOURCES
    EXCLUDE REGEX "/src/model/TrafficPattern\\.cpp$"
)
list(
    FILTER NETMIND_CORE_SOURCES
    EXCLUDE REGEX "/src/model/Packet\\.cpp$"
)

if(NOT NETMIND_CORE_SOURCES)
    message(FATAL_ERROR "No C++ source files found in src/")
endif()

add_library(netmind_core STATIC ${NETMIND_CORE_SOURCES})

target_include_directories(
    netmind_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    netmind_core
    PUBLIC nlohmann_json::nlohmann_json
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/netmind_cli.cpp")
    add_executable(netmind_cli apps/netmind_cli.cpp)
    target_link_libraries(netmind_cli PRIVATE netmind_core)
endif()

file(
    GLOB_RECURSE NETMIND_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

list(
    FILTER NETMIND_TEST_SOURCES
    EXCLUDE REGEX "/bench/"
)

if(BUILD_TESTING AND NETMIND_TEST_SOURCES)
    add_executable(netmind_tests ${NETMIND_TEST_SOURCES})

    target_compile_definitions(
        netmind_tests
        PRIVATE
        "NETMIND_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    )

    target_link_libraries(
        netmind_tests
        PRIVATE
        netmind_core
        Catch2::Catch2WithMain
    )

    include(Catch)
    catch_discover_tests(netmind_tests)
endif()

option(
    NETMIND_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

if(
    NETMIND_ENABLE_SANITIZERS
    AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
)
    set(NETMIND_SANITIZER_TARGETS netmind_core)

    if(TARGET netmind_cli)
        list(APPEND NETMIND_SANITIZER_TARGETS netmind_cli)
    endif()

    if(TARGET netmind_tests)
        list(APPEND NETMIND_SANITIZER_TARGETS netmind_tests)
    endif()

    foreach(target IN LISTS NETMIND_SANITIZER_TARGETS)
        target_compile_options(
            ${target}
            PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )

        target_link_options(
            ${target}
            PRIVATE
            -fsanitize=address,undefined
        )
    endforeach()
endif()
