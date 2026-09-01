file(GLOB_RECURSE BENCHMARK_SOURCES CONFIGURE_DEPENDS "*.cc" "*.cpp")

add_executable(benchmark_enigma ${BENCHMARK_SOURCES})

target_link_libraries(benchmark_enigma
  PRIVATE
    enigmadb_lib
    benchmark::benchmark_main
)
