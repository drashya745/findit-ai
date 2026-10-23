# Vendored third-party code

Both libraries are single-header, MIT-licensed, and copied unmodified from their official
releases. Neither is a paid service. Check integrity with `shasum -a 256 <file>`.

| Library | Version | Files | Licence | Source | SHA-256 |
|---|---|---|---|---|---|
| nlohmann/json | 3.11.3 | `nlohmann/json.hpp` | MIT (SPDX header in the file) | https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp | `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` |
| cpp-httplib | 0.18.3 (released 2024-12-03) | `httplib/httplib.h`, `httplib/LICENSE` | MIT, © 2017 yhirose (`httplib/LICENSE`) | https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz (tarball SHA-256 `a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e`) | `httplib.h`: `a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5`; `LICENSE`: `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b` |

**Where they're used**
- `json.hpp`: the C++ ↔ Python protocol (`src/PythonTextClient.cpp`) and the HTTP API
  (`src/ApiHandlers.cpp`).
- `httplib.h`: only the `findit_server` executable (`tools/findit_server.cpp`). It's built
  without TLS (`CPPHTTPLIB_OPENSSL_SUPPORT` is not defined) and serves only `127.0.0.1`.

**Updating:** download the new release from the URLs above, replace the files, update this
table, and re-run `ctest --test-dir build`.
