#pragma once

//-----------------------------------------------------------------------------
// Minimal test runner: one line per test, and the details only for the checks that fail.
//-----------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace TestRunner {

    //---------------------------------
    struct Test {
        const char  *name;
        void        (*function)();
    };

    //---------------------------------
    inline std::vector<Test> &
    GetTests() {
        static std::vector<Test> tests;
        return tests;
    }

    //---------------------------------
    inline std::vector<std::string> &
    GetFailures() {
        static std::vector<std::string> failures;
        return failures;
    }

    //---------------------------------
    inline int &
    GetNumChecks() {
        static int numChecks = 0;
        return numChecks;
    }

    //---------------------------------
    inline void (*&GetBeforeEach())() {
        static void (*beforeEach)() = nullptr;
        return beforeEach;
    }

    //---------------------------------
    struct Register {
        Register(const char *name, void (*function)()) { GetTests().push_back(Test { name, function }); }
    };

    // Runs before each test, to give it a clean state when all the tests run in the same process
    //---------------------------------
    struct BeforeEach {
        explicit BeforeEach(void (*function)()) { GetBeforeEach() = function; }
    };

    //---------------------------------
    inline void
    Check(bool ok, const char *expression, const char *file, int line) {
        ++GetNumChecks();
        if (!ok) {
            const char *name = file;
            for (const char *c = file; *c != '\0'; ++c) {
                if (*c == '/' || *c == '\\') {
                    name = c + 1;
                }
            }

            char buffer[512];
            snprintf(buffer, sizeof(buffer), "%s:%d: %s", name, line, expression);
            GetFailures().push_back(buffer);
        }
    }

    // With a test name as first argument, it runs only that test, so ctest can run each test in its own process
    //---------------------------------
    inline int
    RunAll(int argc = 0, char *argv[] = nullptr) {
        const char  *only     = argc > 1 ? argv[1] : nullptr;
        size_t      numRun    = 0;
        size_t      numFailed = 0;

        for (const Test &test : GetTests()) {
            if (only != nullptr && strcmp(test.name, only) != 0) {
                continue;
            }

            ++numRun;
            GetFailures().clear();
            GetNumChecks() = 0;

            if (GetBeforeEach() != nullptr) {
                GetBeforeEach()();
            }

            try {
                test.function();
            }
            catch (const std::exception &e) {
                GetFailures().push_back(std::string("unexpected exception: ") + e.what());
            }
            catch (...) {
                GetFailures().push_back("unexpected unknown exception");
            }

            bool ok = GetFailures().empty();
            printf("[%s] %s (%d checks)\n", ok ? " OK " : "FAIL", test.name, GetNumChecks());
            for (const std::string &failure : GetFailures()) {
                printf("         %s\n", failure.c_str());
            }

            if (!ok) {
                ++numFailed;
            }
        }

        // A wrong name must not look like a test that passed
        if (only != nullptr && numRun == 0) {
            printf("No test named %s\n", only);
            return 1;
        }

        printf("\n%zu tests, %zu failed\n", numRun, numFailed);
        return numFailed == 0 ? 0 : 1;
    }

} // end of namespace

#define TEST(name) \
    static void name(); \
    static TestRunner::Register name##Register(#name, name); \
    static void name()

// Variadic, so the expression can have commas, like a == std::vector<int>{1, 2}
#define CHECK(...)          TestRunner::Check((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)
