#include <exception>
#include <iostream>

#include "TestFramework.h"

namespace testfw {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

int& checkCount() {
    static int count = 0;
    return count;
}

int& failureCount() {
    static int count = 0;
    return count;
}

int runAll() {
    int failedTests = 0;
    for (const auto& test : registry()) {
        const int failuresBefore = failureCount();
        try {
            test.run();
        } catch (const std::exception& error) {
            ++failureCount();
            std::cerr << "  unexpected exception: " << error.what() << '\n';
        }
        const bool passed = failureCount() == failuresBefore;
        failedTests += passed ? 0 : 1;
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << '\n';
    }
    const auto total = registry().size();
    std::cout << '\n'
              << total - static_cast<std::size_t>(failedTests) << '/' << total << " tests passed, "
              << checkCount() << " checks, " << failureCount() << " failures\n";
    return failureCount() == 0 ? 0 : 1;
}

}  // namespace testfw
