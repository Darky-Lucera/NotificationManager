# One executable for each file, and one ctest test for each TEST(...), so each test runs in its own process
#--------------------------------------
set(NOTIFICATIONMANAGER_TEST_FILES
    AllocTests
    ApiTests
    ClearTests
    GlobalObjectsTests
    OverwriteTests
    PayloadTests
    SendTests
    ThreadTests
)

# Clang's ThreadSanitizer already defines the global operator new that AllocTests replaces.
# That test checks memory, not threads, so no ThreadSanitizer build runs it
if (CMAKE_CXX_FLAGS MATCHES "fsanitize=thread")
    list(REMOVE_ITEM NOTIFICATIONMANAGER_TEST_FILES AllocTests)
endif()

foreach(file ${NOTIFICATIONMANAGER_TEST_FILES})
    add_executable(${file}
        ${file}.cpp
        TestCommon.h
        TestRunner.h
    )

    notificationmanager_setup_target(${file})

    target_link_libraries(${file}
    PRIVATE
        NotificationManager::NotificationManager
    )

    set_target_properties(${file}
    PROPERTIES
        FOLDER Tests
    )

    # Changing the file runs CMake again, so new tests are added
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/${file}.cpp)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})

    string(REGEX REPLACE "Tests$" "" suite ${file})
    file(STRINGS ${source} lines REGEX "^TEST\\(")
    foreach(line ${lines})
        string(REGEX REPLACE "^TEST\\(([A-Za-z0-9_]+)\\).*" "\\1" name "${line}")
        add_test(NAME ${suite}.${name} COMMAND ${file} ${name})

        # Some bugs make the test hang, so a timeout counts as a failure
        set_tests_properties(${suite}.${name}
        PROPERTIES
            TIMEOUT 10
        )
    endforeach()
endforeach()
