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

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

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

# --- UI dependencies ---
# Qt 6 comes from the system or aqtinstall, NEVER FetchContent (building Qt from
# source is impractical). Widgets for the UI, Test for headless UI unit tests.
# Only required when building the UI/app/UI-tests; a core-only build needs no Qt.
if(GITGUI_BUILD_UI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
endif()
