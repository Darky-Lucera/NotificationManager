cmake_minimum_required(VERSION 3.16)
project(NotificationManager VERSION 2.0.0 LANGUAGES CXX)

# Options
#--------------------------------------
# A project that adds this one with add_subdirectory only builds the library, unless it asks for more.
# PROJECT_IS_TOP_LEVEL would do the same, but it needs CMake 3.21.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(NOTIFICATIONMANAGER_IS_TOP_LEVEL ON)
else()
    set(NOTIFICATIONMANAGER_IS_TOP_LEVEL OFF)
endif()
option(NOTIFICATIONMANAGER_BUILD_EXAMPLES "Build the examples" ${NOTIFICATIONMANAGER_IS_TOP_LEVEL})
option(NOTIFICATIONMANAGER_BUILD_TESTS    "Build the tests"    ${NOTIFICATIONMANAGER_IS_TOP_LEVEL})

# By default the library supports C++11, with its own copy of any (linb::any).
# Projects that use C++17 or later should turn this on, because std::any comes with the compiler
option(NOTIFICATIONMANAGER_USE_STD_ANY "Use std::any, which needs C++17, instead of linb::any" OFF)

# Only for the main project
#--------------------------------------
if (NOTIFICATIONMANAGER_IS_TOP_LEVEL)
    # The oldest standard the library supports, so the examples and the tests check it
    set(CMAKE_CXX_STANDARD          11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS        OFF)

    # Makefile and Ninja builds have no optimization without a build type.
    if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()

    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# Dependencies
#--------------------------------------
find_package(Threads REQUIRED)

# Target options
#--------------------------------------
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles([=[
        #if !defined(_M_IX86) && !defined(_M_X64)
            #error Edit and Continue is not supported on this architecture
        #endif
        int main() { return 0; }
    ]=] NOTIFICATIONMANAGER_MSVC_X86_OR_X64)

    # Edit and Continue in Debug. CMake 3.16 has no target property that can replace /Zi with /ZI.
    if (NOTIFICATIONMANAGER_MSVC_X86_OR_X64)
        string(REGEX REPLACE "[/-]Z[7iI]" "/ZI" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        if (NOT CMAKE_CXX_FLAGS_DEBUG MATCHES "[/-]ZI")
            string(APPEND CMAKE_CXX_FLAGS_DEBUG " /ZI")
        endif()
    endif()
endif()

function(notificationmanager_setup_target target)
    if (MSVC)
        target_compile_definitions(${target}
        PRIVATE
            _CRT_SECURE_NO_WARNINGS
        )

        target_compile_options(${target}
        PRIVATE
            /W4 /wd4100
            /Zc:__cplusplus
            /utf-8
        )

        if (MSVC_VERSION GREATER_EQUAL 1900)
            target_compile_options(${target}
            PRIVATE
                /sdl
            )
        endif()
    else()
        target_compile_options(${target}
        PRIVATE
            -Wall -Wextra
            -Wno-switch -Wno-unused-function -Wno-unused-parameter
        )
    endif()
endfunction()

# Library
#--------------------------------------
set(SRC_Notifications
    ${CMAKE_CURRENT_SOURCE_DIR}/notifications/Delegate.h
    ${CMAKE_CURRENT_SOURCE_DIR}/notifications/NotificationManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/notifications/NotificationManager.h
    # Each project has its own NotificationId.h. NotificationManager.cpp only needs its declaration.
)

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${SRC_Notifications}
)

add_library(NotificationManager STATIC
    ${SRC_Notifications}
)

add_library(NotificationManager::NotificationManager ALIAS NotificationManager)

notificationmanager_setup_target(NotificationManager)

if (NOTIFICATIONMANAGER_USE_STD_ANY)
    # PUBLIC, so the code that uses the library gets the same any and at least C++17
    target_compile_definitions(NotificationManager
    PUBLIC
        NOTIFICATIONMANAGER_USE_STD_ANY
    )

    target_compile_features(NotificationManager
    PUBLIC
        cxx_std_17
    )
else()
    target_compile_features(NotificationManager
    PUBLIC
        cxx_std_11
    )
endif()

target_include_directories(NotificationManager
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NotificationManager
PUBLIC
    Threads::Threads
)

# Examples
#--------------------------------------
function(notificationmanager_add_example target source_dir)
    set(sources
        ${ARGN}
    )

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/${source_dir}"
        FILES ${sources}
    )

    add_executable(${target}
        ${sources}
    )

    notificationmanager_setup_target(${target})

    target_link_libraries(${target}
    PRIVATE
        NotificationManager::NotificationManager
    )

    set_target_properties(${target}
    PROPERTIES
        FOLDER Examples
    )
endfunction()

if (NOTIFICATIONMANAGER_BUILD_EXAMPLES)
    notificationmanager_add_example(GameEvents examples/GameEvents
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/GameEvents/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/GameEvents/NotificationId.h
    )

    notificationmanager_add_example(BackgroundLoading examples/BackgroundLoading
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/BackgroundLoading/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/BackgroundLoading/NotificationId.h
    )
endif()

# Tests
#--------------------------------------
if (NOTIFICATIONMANAGER_BUILD_TESTS)
    # include(CTest) writes the DartConfiguration.tcl that some tools need to run ctest, like CMake Tools in VS Code.
    # The property stops it from adding the dashboard targets (Continuous, Experimental, Nightly...)
    set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
    include(CTest)
    add_subdirectory(tests)
endif()
