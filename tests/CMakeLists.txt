include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cc")

add_executable(unit_tests ${TEST_SOURCES})

target_link_libraries(unit_tests
  PRIVATE
    enigmadb_lib
    GTest::gtest_main
)

gtest_discover_tests(unit_tests)

target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)

if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "21")
    # Workaround for a googletest bug with char conversions recognized by Clang 21+
    # See https://github.com/google/googletest/issues/4762
    # TODO: remove when googletest is updated

    if(TARGET gtest)
      target_compile_options(gtest PRIVATE -Wno-character-conversion)
    endif()

    if(TARGET gtest_main)
      target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
    endif()

  endif()
endif()

