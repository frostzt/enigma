include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES "*.cc")

add_executable(unit_tests ${TEST_SOURCES})

target_link_libraries(unit_tests
  PRIVATE
    enigmadb_lib
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(unit_tests)
