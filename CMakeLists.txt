cmake_minimum_required(VERSION 3.10)
project(NotificationCenter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Compiler flags
#--------------------------------------
if(MSVC)
    # Security check
    add_compile_options(/GS)
    # Function level linking
    add_compile_options(/Gy)
    # Exceptions
    add_compile_options(/EHsc)
    # Enable Minimal Rebuild (required for Edit and Continue)
    add_compile_options(/Gm)
    # SDL checks 2015+
    if(MSVC_VERSION GREATER_EQUAL 1900)
        add_compile_options(/sdl)
    endif()
    add_compile_options(/fp:fast)
    # Runtime library
    add_compile_options("$<IF:$<CONFIG:Debug>,/MDd,/MD>")
    # Program database for edit and continue
    add_compile_options("$<IF:$<CONFIG:Debug>,/ZI,/Zi>")
    # Optimizations
    add_compile_options("$<IF:$<CONFIG:Debug>,/Od,/O2>")
    # Inline function expansion
    add_compile_options("$<IF:$<CONFIG:Debug>,/Ob0,/Ob2>")
    # Basic runtime checks
    add_compile_options("$<$<CONFIG:Debug>:/RTC1>")
    # Enable intrinsics
    add_compile_options("$<$<CONFIG:RELEASE>:/Oi>")
else()
    add_compile_options("$<$<CONFIG:Debug>:-g>")
    add_compile_options("$<IF:$<CONFIG:Debug>,-O0,-O2>")
    add_compile_options(-Wall -Wextra)
    add_compile_options(-Wno-switch -Wno-unused-function -Wno-unused-parameter)
    add_compile_options(-ffast-math)
    if (UNIX)
        add_compile_options(-pthread)
        link_libraries(-lpthread)
    endif()
endif()

#--------------------------------------
set(NOTIFICATIONS
    notifications/Delegate.h
    notifications/NotificationManager.cpp
    notifications/NotificationManager.h
    #notifications/NotificationId.h     Use per project NotificationId.h
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${NOTIFICATIONS})

#--------------------------------------
set(Example1
    example1/main.cpp
    example1/NotificationId.h
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/example1" FILES ${Example1})

add_executable(Example1
    ${Example1}
    ${NOTIFICATIONS}
)
target_include_directories(Example1 PRIVATE .)

#--------------------------------------
set(Example2
    example2/main.cpp
    example2/agent.h
    example2/NotificationId.h
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/example2" FILES ${Example2})

add_executable(Example2
    ${Example2}
    ${NOTIFICATIONS}
)
target_include_directories(Example2 PRIVATE .)
