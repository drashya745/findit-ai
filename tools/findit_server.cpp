// findit_server: local JSON API over the same AppContext the CLI uses.
//
//   findit_server [--port N] [--data-dir DIR] [--matching-config PATH]
//                 [--text-engine python|keyword] [--sample] [--allow-origin URL]
//
// Security model (local, single-user tool):
// - Listens on 127.0.0.1 only (never 0.0.0.0); there is no option to change the address.
// - Accepts only requests whose Host header is 127.0.0.1:<port> or localhost:<port>, which
//   blocks DNS-rebinding attacks from web pages.
// - Serves only /api/* JSON endpoints: no static files, no file paths or commands from
//   requests. Bodies are capped at 16 KB. Errors never include local file paths.
// - CORS is off unless --allow-origin names an exact http://localhost or http://127.0.0.1 origin.
// - One request is handled at a time (AppContext is not thread-safe).
// - The data-directory lock is held for the whole run and released on SIGINT/SIGTERM.

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <regex>
#include <string>
#include <thread>

#include <httplib/httplib.h>

#include "findit/ApiHandlers.h"
#include "findit/AppContext.h"

using findit::ApiHandlers;
using findit::ApiRequest;
using findit::ApiResponse;
using findit::AppContext;

namespace {

constexpr int kDefaultPort = 8080;
constexpr int kExitLocked = 3;

struct ServerOptions {
    findit::AppOptions app;
    int port = kDefaultPort;
    std::string allowOrigin;
};

void usage() {
    std::cerr << "Usage: findit_server [--port N] [--data-dir DIR] [--matching-config PATH]\n"
                 "                     [--text-engine python|keyword] [--sample] [--allow-origin URL]\n"
                 "  --port          1024-65535 (default 8080); 0 picks a free port\n"
                 "  --allow-origin  e.g. http://localhost:5173 (only needed without the Vite proxy)\n";
}

bool parseArgs(int argc, char** argv, ServerOptions& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--sample") {
            o.app.sampleOnly = true;
        } else if (arg == "--port" && hasValue) {
            const std::string value = argv[++i];
            if (value.empty() || value.size() > 5 || value.find_first_not_of("0123456789") != std::string::npos) {
                return false;
            }
            o.port = std::stoi(value);
            if (o.port != 0 && (o.port < 1024 || o.port > 65535)) {
                return false;
            }
        } else if (arg == "--data-dir" && hasValue) {
            o.app.dataDir = argv[++i];
        } else if (arg == "--matching-config" && hasValue) {
            o.app.matchingConfig = argv[++i];
        } else if (arg == "--text-engine" && hasValue) {
            o.app.textEngine = argv[++i];
        } else if (arg == "--allow-origin" && hasValue) {
            o.allowOrigin = argv[++i];
            static const std::regex kLocalOrigin(R"(^http://(localhost|127\.0\.0\.1)(:[0-9]{1,5})?$)");
            if (!std::regex_match(o.allowOrigin, kLocalOrigin)) {
                std::cerr << "--allow-origin must be http://localhost[:port] or http://127.0.0.1[:port]\n";
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

void writeJson(httplib::Response& res, int status, const nlohmann::json& body) {
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                    "application/json; charset=utf-8");
}

void logLine(const std::string& line) {
    std::cerr << line << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    ServerOptions options;
    if (!parseArgs(argc, argv, options)) {
        usage();
        return 2;
    }
    options.app.programName = "findit_server";

    findit::MatchingConfig config;
    try {
        std::string note;
        config = AppContext::loadMatchingConfig(options.app.matchingConfig, note);
        if (!note.empty()) {
            logLine(note);
        }
    } catch (const std::exception& error) {
        std::cerr << "Invalid matching config: " << error.what() << '\n';
        return 1;
    }
    if (!AppContext::applyTextEngine(options.app.textEngine, config)) {
        std::cerr << "--text-engine must be python or keyword\n";
        return 2;
    }

    // Block SIGINT/SIGTERM in every thread; a dedicated thread waits for them below.
    // Reset their dispositions first: shells start background jobs with SIGINT *ignored*,
    // and an ignored signal is discarded before sigwait() can receive it.
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    AppContext context(options.app, config, [](const std::string& reason) {
        logLine("[!] Python matcher unavailable: " + reason + " -- using the keyword baseline for this session.");
    });
    try {
        const findit::LoadSummary loaded = context.load();
        logLine(loaded.usedSampleData
                ? "Using built-in fictional sample data (nothing is saved)."
                : "Loaded " + std::to_string(loaded.users) + " users, " + std::to_string(loaded.reports) +
                      " reports and " + std::to_string(loaded.matches) + " confirmed match(es) from " +
                      options.app.dataDir + "/");
    } catch (const findit::DataDirLockedError& error) {
        std::cerr << error.what() << '\n';
        return kExitLocked;
    } catch (const std::exception& error) {
        std::cerr << "Failed to load data: " << error.what() << '\n';
        return 1;
    }

    ApiHandlers api(context, logLine);
    std::mutex serviceMutex;  // AppContext is not thread-safe: one request at a time.
    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(4); };
    server.set_payload_max_length(ApiHandlers::kMaxBodyBytes);
    server.set_read_timeout(5, 0);
    server.set_write_timeout(5, 0);
    server.set_keep_alive_max_count(50);
    server.set_default_headers({{"X-Content-Type-Options", "nosniff"},
                                {"Cache-Control", "no-store"},
                                {"Referrer-Policy", "no-referrer"}});

    std::atomic<int> boundPort{0};
    const auto hostAllowed = [&boundPort](const httplib::Request& req) {
        const std::string host = req.get_header_value("Host");
        const std::string port = std::to_string(boundPort.load());
        return host == "127.0.0.1:" + port || host == "localhost:" + port;
    };
    const auto addCors = [&options](const httplib::Request& req, httplib::Response& res) {
        if (!options.allowOrigin.empty() && req.get_header_value("Origin") == options.allowOrigin) {
            res.set_header("Access-Control-Allow-Origin", options.allowOrigin);
            res.set_header("Vary", "Origin");
        }
    };

    const auto handler = [&](const httplib::Request& req, httplib::Response& res) {
        const auto started = std::chrono::steady_clock::now();
        ApiResponse response;
        if (!hostAllowed(req)) {
            response = {403, {{"error", {{"code", "forbidden_host"},
                                         {"message", "Requests must be addressed to 127.0.0.1 or localhost"}}}}};
        } else {
            ApiRequest request{req.method, req.path, {}, req.get_header_value("Content-Type"), req.body};
            for (const auto& param : req.params) {
                request.query.insert(param);
            }
            std::lock_guard<std::mutex> guard(serviceMutex);
            response = api.handle(request);
        }
        addCors(req, res);
        writeJson(res, response.status, response.body);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count();
        logLine(req.method + " " + req.path + " -> " + std::to_string(response.status) + " (" + std::to_string(ms) + " ms)");
    };
    server.Get(".*", handler);
    server.Post(".*", handler);
    server.Put(".*", handler);
    server.Patch(".*", handler);
    server.Delete(".*", handler);
    server.Options(".*", [&](const httplib::Request& req, httplib::Response& res) {
        if (!options.allowOrigin.empty() && req.get_header_value("Origin") == options.allowOrigin) {
            addCors(req, res);
            res.set_header("Access-Control-Allow-Methods", "GET, POST");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.set_header("Access-Control-Max-Age", "600");
            res.status = 204;
        } else {
            writeJson(res, 403, {{"error", {{"code", "cors_not_allowed"}, {"message", "Origin not allowed"}}}});
        }
    });
    // Errors produced by cpp-httplib itself (e.g. 413 body too large, malformed requests).
    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            const std::string code = res.status == 413 ? "payload_too_large" : "bad_request";
            writeJson(res, res.status, {{"error", {{"code", code}, {"message", httplib::status_message(res.status)}}}});
        }
    });
    server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        writeJson(res, 500, {{"error", {{"code", "internal_error"}, {"message", "Unexpected server error"}}}});
    });

    if (options.port == 0) {
        boundPort = server.bind_to_any_port("127.0.0.1");
    } else if (server.bind_to_port("127.0.0.1", options.port)) {
        boundPort = options.port;
    }
    if (boundPort <= 0) {
        std::cerr << "Cannot listen on 127.0.0.1:" << options.port << " (port in use?)\n";
        return 1;
    }

    std::thread signalThread([&server, &signals] {
        int received = 0;
        sigwait(&signals, &received);
        logLine(std::string("Received ") + (received == SIGINT ? "SIGINT" : "SIGTERM") + "; shutting down.");
        server.stop();
    });

    std::cout << "FindIt API listening on http://127.0.0.1:" << boundPort.load() << " (data: "
              << context.dataSource() << "; matcher: " << context.engine().strategyName() << ")" << std::endl;
    const bool ok = server.listen_after_bind();

    // If listening ended for another reason, wake the signal thread so it can be joined.
    pthread_kill(signalThread.native_handle(), SIGTERM);
    signalThread.join();
    logLine("Server stopped; releasing the data-directory lock.");
    return ok ? 0 : 1;
}
