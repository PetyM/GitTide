include(FetchContent)

# --- Core dependencies (always built) ---
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)   # libgit2's own tests
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(USE_SSH OFF CACHE BOOL "" FORCE)
set(USE_HTTPS OFF CACHE BOOL "" FORCE)      # no network ops in this milestone; avoids OpenSSL/mbedTLS dep
FetchContent_Declare(
  libgit2
  GIT_REPOSITORY https://github.com/libgit2/libgit2.git
  GIT_TAG        v1.8.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libgit2)

# --- Test-only dependencies ---
if(GITGUI_BUILD_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(Catch2)
endif()
