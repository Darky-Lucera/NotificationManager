cmake_minimum_required(VERSION 3.16)
project(NotificationCenter VERSION 1.3.0 LANGUAGES CXX)

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

# Only for the main project
#--------------------------------------
if (NOTIFICATIONMANAGER_IS_TOP_LEVEL)
    # NotificationManager.h uses std::any from C++17 on, and its own any before, so the library and the code that
    # uses it must be built with the same standard. As a subproject, the library takes the one of the main project.
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
            /W3
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

notificationmanager_setup_target(NotificationManager)

target_compile_features(NotificationManager
PUBLIC
    cxx_std_11
)

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
        NotificationManager
    )
endfunction()

if (NOTIFICATIONMANAGER_BUILD_EXAMPLES)
    notificationmanager_add_example(Example1 example1
        ${CMAKE_CURRENT_SOURCE_DIR}/example1/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/example1/NotificationId.h
    )

    notificationmanager_add_example(Example2 example2
        ${CMAKE_CURRENT_SOURCE_DIR}/example2/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/example2/agent.h
        ${CMAKE_CURRENT_SOURCE_DIR}/example2/NotificationId.h
    )

    notificationmanager_add_example(Example3 example3
        ${CMAKE_CURRENT_SOURCE_DIR}/example3/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/example3/NotificationId.h
    )
endif()
