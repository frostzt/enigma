# --- External Content ---

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
  SYSTEM
)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        main
  SYSTEM
)

# Populate the content
FetchContent_MakeAvailable(googletest googlebenchmark)
