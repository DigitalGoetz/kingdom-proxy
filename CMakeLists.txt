cmake_minimum_required(VERSION 3.20)
project(kingdom-proxy CXX C)  # C is needed to compile the vendored sqlite3.c amalgamation.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(KINGDOM_PROXY_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)

add_library(kingdom_proxy_core STATIC
  src/util/logger.hpp
  src/util/env.hpp
  src/model/route.hpp
  src/db/database.hpp
  src/db/database.cpp
  src/docker/docker_client.hpp
  src/docker/docker_client.cpp
  src/nginx/config_generator.hpp
  src/nginx/config_generator.cpp
  src/nginx/nginx_controller.hpp
  src/nginx/nginx_controller.cpp
  src/worker/sync_worker.hpp
  src/worker/sync_worker.cpp
  src/api/server.hpp
  src/api/server.cpp
)

target_include_directories(kingdom_proxy_core PUBLIC src)

target_link_libraries(kingdom_proxy_core PUBLIC
  httplib::httplib
  nlohmann_json::nlohmann_json
  sqlite3
  Threads::Threads
)

add_executable(kingdom-proxy-api src/main.cpp)
target_link_libraries(kingdom-proxy-api PRIVATE kingdom_proxy_core)

if(KINGDOM_PROXY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
