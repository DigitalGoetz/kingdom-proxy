include(FetchContent)
FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_executable(kingdom_proxy_tests
  test_config_generator.cpp
  test_database.cpp
)
target_link_libraries(kingdom_proxy_tests PRIVATE kingdom_proxy_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(kingdom_proxy_tests)
