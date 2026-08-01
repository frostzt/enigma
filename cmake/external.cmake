# --- External Content ---

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
  SYSTEM
)

# Populate the content
FetchContent_MakeAvailable(googletest)
