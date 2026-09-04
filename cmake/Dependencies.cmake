# Third-party dependencies, fetched at configure time so the project builds
# from a clean checkout with nothing but a C++20 toolchain, CMake, and git.
# Everything here is pinned to an exact tag/version for reproducible builds.

include(FetchContent)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)  # Re-extract on URL change rather than keeping archive timestamps.
endif()

find_package(Threads REQUIRED)

# --- cpp-httplib: header-only HTTP client/server, used for both the admin
# API and as an HTTP-over-unix-socket client for the Docker Engine API. -----
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.5
  GIT_SHALLOW    TRUE
)

# --- nlohmann/json: header-only JSON for request/response bodies and for
# talking to the Docker Engine API. ------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(httplib nlohmann_json)

# --- SQLite3: vendored as the official amalgamation (sqlite3.c/.h) so the
# proxy's route store has zero runtime dependency on a system libsqlite3. ---
FetchContent_Declare(
  sqlite3_amalgamation
  URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
  URL_HASH SHA256=ea170e73e447703e8359308ca2e4366a3ae0c4304a8665896f068c736781c651
)
FetchContent_MakeAvailable(sqlite3_amalgamation)

add_library(sqlite3 STATIC
  ${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c
)
target_include_directories(sqlite3 SYSTEM PUBLIC ${sqlite3_amalgamation_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_ENABLE_FTS5=0
  SQLITE_OMIT_LOAD_EXTENSION=1
)
target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
