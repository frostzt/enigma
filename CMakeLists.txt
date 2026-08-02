cmake_minimum_required(VERSION 3.25)
project(enigmadb VERSION 0.0.1 LANGUAGES CXX)

# --- Options ---

option(ENIGMADB_BUILD_TOOLS "build developer/debug tools" OFF)

# --- CMake Flags ---

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Compile Flags ---

add_compile_options(
  -Wall -Wextra -Werror -Wpedantic -Wreorder
)
add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)

# --- Build Flags and stuff ---

# Default to Debug if CMAKE_BUILD_TYPE isn't specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-O0 -g)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  add_compile_definitions(DEBUG)
else()
  add_compile_definitions(NDEBUG)
  add_compile_options(-O3)
endif()

# --- Testing ---

enable_testing()

# --- Modules ---

include(cmake/external.cmake)

# --- Directories ---

# Source files
add_subdirectory(src)

# Docs
add_subdirectory(docs)

# Test files
add_subdirectory(tests)

if (ENIGMADB_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

# --- Project Targets ---
