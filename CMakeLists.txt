cmake_minimum_required(VERSION 3.20)
project(FindItAI VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Core library: everything except the executables' entry points, so tests link the same code.
add_library(findit_core STATIC
  # Phase 1: domain
  src/Date.cpp
  src/Validation.cpp
  src/Types.cpp
  src/User.cpp
  src/ItemReport.cpp
  src/LostReport.cpp
  src/FoundReport.cpp
  src/MatchResult.cpp
  src/MatchEngine.cpp
  src/ReportRepository.cpp
  src/ReportService.cpp
  src/SampleData.cpp
  # Phase 2: ingestion and persistence
  src/KeyValueConfig.cpp
  src/Csv.cpp
  src/Sha256.cpp
  src/DatasetConfig.cpp
  src/DatasetAdapter.cpp
  src/DatasetLoader.cpp
  src/IngestSummary.cpp
  src/ReportCsvStore.cpp
  # Phase 3: baseline matching
  src/TextKeywords.cpp
  src/MatchingConfig.cpp
  src/KeywordBaselineStrategy.cpp
  # Phase 5: Python integration
  src/PythonProcess.cpp
  src/PythonTextClient.cpp
  src/PythonTfidfStrategy.cpp
  # Shared application startup and persistence (CLI and HTTP server)
  src/AppContext.cpp
  src/DataDirLock.cpp
  # HTTP API logic (transport-independent; the server adapter is tools/findit_server.cpp)
  src/ApiHandlers.cpp
)
target_include_directories(findit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Vendored nlohmann/json 3.11.3 (MIT, single header). SYSTEM silences its warnings.
target_include_directories(findit_core SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_options(findit_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)

add_executable(findit src/main.cpp)
target_link_libraries(findit PRIVATE findit_core)

add_executable(findit_ingest tools/findit_ingest.cpp)
target_link_libraries(findit_ingest PRIVATE findit_core)

add_executable(findit_eval tools/findit_eval.cpp)
target_link_libraries(findit_eval PRIVATE findit_core)

# Local JSON API (cpp-httplib 0.18.3, vendored in third_party/httplib, MIT; no TLS).
find_package(Threads REQUIRED)
add_executable(findit_server tools/findit_server.cpp)
target_link_libraries(findit_server PRIVATE findit_core Threads::Threads)

enable_testing()
add_executable(findit_tests
  tests/TestFramework.cpp
  tests/test_main.cpp
  tests/test_domain.cpp
  tests/test_ingest.cpp
  tests/test_matching.cpp
  tests/test_python_protocol.cpp
  tests/test_app_context.cpp
  tests/test_data_dir_lock.cpp
  tests/test_api.cpp
)
target_link_libraries(findit_tests PRIVATE findit_core)
# Tests read config/ and tests/fixtures/ from the source tree.
target_compile_definitions(findit_tests PRIVATE FINDIT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME findit_tests COMMAND findit_tests)

# End-to-end tests against the real Python service. Reported as SKIPPED (exit 77) when the
# interpreter or its dependencies are missing. Override with -DFINDIT_PYTHON=/path/to/python.
set(FINDIT_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python" CACHE FILEPATH
    "Python interpreter with ml/requirements.txt installed")
add_executable(findit_integration_tests
  tests/TestFramework.cpp
  tests/integration/integration_main.cpp
  tests/integration/test_python_integration.cpp
)
target_link_libraries(findit_integration_tests PRIVATE findit_core)
target_compile_definitions(findit_integration_tests PRIVATE
  FINDIT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" FINDIT_PYTHON="${FINDIT_PYTHON}")
add_test(NAME findit_integration_tests COMMAND findit_integration_tests)

# HTTP end-to-end test: real findit_server on 127.0.0.1, restart and persistence. Uses the
# venv Python only to parse JSON responses (falls back to python3 on PATH).
add_test(NAME findit_server_tests
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/server/test_server_e2e.sh
          $<TARGET_FILE:findit_server> $<TARGET_FILE:findit> ${CMAKE_CURRENT_SOURCE_DIR} ${FINDIT_PYTHON})

# Menu-level test: drives the real CLI through stdin (keyword engine, no Python needed).
add_test(NAME findit_cli_tests
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/test_cli_confirm.sh
          $<TARGET_FILE:findit> ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(findit_integration_tests PROPERTIES SKIP_RETURN_CODE 77)
