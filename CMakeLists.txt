cmake_minimum_required(VERSION 3.20)
project(enigmadb)

# --- CMake Flags ---

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Compile Flags ---

add_compile_options(
  -Wall -Wextra -Werror -Wpedantic
)
add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)

# --- Build Flags and stuff ---

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-O0 -g)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  add_compile_definitions(DEBUG)
endif()

# --- Testing ---

enable_testing()

# --- Modules ---

include(cmake/external.cmake)

# --- Directories ---

# Source files
add_subdirectory(src)

# Test files
add_subdirectory(tests)

# --- Project Targets ---
