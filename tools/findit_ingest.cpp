// findit_ingest: reads the raw dataset (read-only), validates and normalizes it, and writes
// canonical users/reports, a rejected-rows log and a reproducible summary to the output dir.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "findit/Csv.h"
#include "findit/DatasetConfig.h"
#include "findit/DatasetLoader.h"
#include "findit/IngestSummary.h"
#include "findit/MatchEngine.h"
#include "findit/ReportCsvStore.h"
#include "findit/ReportRepository.h"
#include "findit/ReportService.h"
#include "findit/Sha256.h"

namespace fs = std::filesystem;
using namespace findit;

namespace {

void printUsage() {
    std::cout << "Usage: findit_ingest [--config PATH] [--input PATH] [--out-dir DIR]\n"
                 "  --config   dataset mapping (default: config/dataset_mapping.conf)\n"
                 "  --input    source CSV (default: source.path from the config)\n"
                 "  --out-dir  output directory (default: data/processed)\n";
}

std::string readBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open input file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = "config/dataset_mapping.conf";
    std::string inputPath;
    std::string outDir = "data/processed";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if ((arg == "--config" || arg == "--input" || arg == "--out-dir") && i + 1 < argc) {
            std::string& target = arg == "--config" ? configPath
                                  : arg == "--input" ? inputPath
                                                     : outDir;
            target = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            printUsage();
            return 2;
        }
    }

    try {
        const DatasetConfig config = DatasetConfig::loadFile(configPath);
        if (inputPath.empty()) {
            inputPath = config.sourcePath();
        }
        if (inputPath.empty()) {
            throw std::runtime_error("No input given: pass --input or set source.path in " +
                                     configPath);
        }
        if (!fs::is_regular_file(inputPath)) {
            throw std::runtime_error("Input file not found: " + inputPath);
        }

        // Never write next to the source file, so the raw dataset cannot be overwritten.
        fs::create_directories(outDir);
        const fs::path inputDir = fs::canonical(inputPath).parent_path();
        if (fs::canonical(outDir) == inputDir) {
            throw std::runtime_error("Refusing to write outputs into the input file's directory (" +
                                     inputDir.string() + "); choose a different --out-dir");
        }

        std::string bytes = readBinaryFile(inputPath);
        SummaryContext context;
        context.inputPath = inputPath;
        context.inputBytes = bytes.size();
        context.inputSha256 = sha256Hex(bytes);
        context.configPath = configPath;

        CsvReader reader(std::move(bytes));
        ReportRepository repository;
        MatchEngine engine;
        ReportService service(repository, engine);
        DatasetLoader loader(config, service);
        const IngestResult result = loader.load(reader);

        const fs::path out(outDir);
        const std::string usersPath = (out / "users.csv").string();
        const std::string reportsPath = (out / "reports.csv").string();
        const std::string rejectedPath = (out / "rejected_rows.csv").string();
        const std::string summaryPath = (out / "summary.json").string();
        context.outputFiles = {{"users", usersPath},
                               {"reports", reportsPath},
                               {"rejected_rows", rejectedPath},
                               {"summary", summaryPath}};
        context.usersWritten = service.listUsers().size();
        context.reportsWritten = service.listReports().size();

        persistence::saveUsers(usersPath, service.listUsers());
        persistence::saveReports(reportsPath, service.listReports());
        persistence::writeFileAtomically(rejectedPath, [&result](std::ostream& o) {
            writeRejectedRowsCsv(o, result.rejectedRows);
        });
        persistence::writeFileAtomically(summaryPath, [&](std::ostream& o) {
            writeSummaryJson(o, result, config, context);
        });

        printSummary(std::cout, result, context);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "findit_ingest: " << error.what() << '\n';
        return 1;
    }
}
