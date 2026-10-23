// Integration test runner: needs the venv Python with ml/requirements.txt installed.
// Exits with 77 (CTest "skipped") and a clear reason when that interpreter is unavailable,
// so a missing venv is reported as SKIPPED, never as a pass.

#include <filesystem>
#include <iostream>

#include "../TestFramework.h"
#include "findit/PythonTextClient.h"

int main() {
    const std::string python = FINDIT_PYTHON;
    if (!std::filesystem::exists(python)) {
        std::cout << "SKIPPED: Python interpreter not found at " << python
                  << "\n  Create it with: python3 -m venv .venv && .venv/bin/pip install -r ml/requirements.txt\n";
        return 77;
    }
    findit::PythonProcessOptions options;
    options.executable = python;
    options.modulePath = std::string(FINDIT_SOURCE_DIR) + "/ml";
    findit::PythonTextClient probe(options);
    try {
        probe.start();
    } catch (const findit::PythonMatcherError& error) {
        std::cout << "SKIPPED: " << error.what() << '\n';
        return 77;
    }
    std::cout << "Using Python at " << python << "\n\n";
    return testfw::runAll();
}
