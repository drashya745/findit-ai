#pragma once

// Tiny dependency-free test framework. Test files register cases with TEST_CASE;
// test_main.cpp runs them all. Run by CTest.

#include <iostream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& registry();
int& checkCount();
int& failureCount();
// Runs every registered test, prints a summary, returns 0 if all passed.
int runAll();

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

}  // namespace testfw

#define TEST_CASE(function, label)                                          \
    static void function();                                                 \
    static const ::testfw::Registrar function##Registrar(label, function); \
    static void function()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++::testfw::checkCount();                                                     \
        if (!(cond)) {                                                                \
            ++::testfw::failureCount();                                               \
            std::cerr << "  " << __FILE__ << ':' << __LINE__ << ": CHECK(" #cond ")\n"; \
        }                                                                             \
    } while (0)

#define CHECK_THROWS_AS(expr, ExceptionType)                                              \
    do {                                                                                  \
        ++::testfw::checkCount();                                                         \
        bool caught = false;                                                              \
        try {                                                                             \
            (void)(expr);                                                                 \
        } catch (const ExceptionType&) {                                                  \
            caught = true;                                                                \
        } catch (...) {                                                                   \
        }                                                                                 \
        if (!caught) {                                                                    \
            ++::testfw::failureCount();                                                   \
            std::cerr << "  " << __FILE__ << ':' << __LINE__                              \
                      << ": expected " #ExceptionType " from " #expr "\n";                \
        }                                                                                 \
    } while (0)

// Absolute tolerance for floating-point score comparisons.
#define CHECK_NEAR(actual, expected) CHECK(std::abs((actual) - (expected)) < 1e-9)
