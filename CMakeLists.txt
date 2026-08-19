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

# Sanitizer selection. ASan and TSan are mutually exclusive at the compiler level,
# so this is ONE choice rather than a bag of flags. Previously ASan+UBSan were
# hardcoded into every Debug build, which meant a TSan build was impossible and the
# CI matrix's "sanitizer_flags" column had no effect -- every Debug job was running
# the same ASan build.
set(ENIGMA_SANITIZER "address" CACHE STRING "Sanitizer for Debug builds: address | thread | none")
set_property(CACHE ENIGMA_SANITIZER PROPERTY STRINGS address thread none)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-O0 -g)
  add_compile_definitions(DEBUG)

  if (ENIGMA_SANITIZER STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  elseif (ENIGMA_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
  elseif (ENIGMA_SANITIZER STREQUAL "none")
    # No sanitizer -- used by the plain GCC/Clang matrix jobs so they actually
    # exercise an unsanitized build rather than duplicating the ASan one.
  else()
    message(FATAL_ERROR "ENIGMA_SANITIZER must be one of: address, thread, none (got '${ENIGMA_SANITIZER}')")
  endif()
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
