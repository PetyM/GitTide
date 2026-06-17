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
# Qt 6 comes from the system or aqtinstall, NEVER FetchContent. Widgets for the UI,
# Test for headless UI unit tests, Concurrent for off-main-thread git ops.
# QCoro adds co_await support over QFuture; it is built from source via FetchContent.
if(GITGUI_BUILD_UI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent Svg)

  # Trim QCoro to the modules we use; the rest pull in Qt components we don't link.
  set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(QCORO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QML OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTDBUS OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTNETWORK OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    qcoro
    GIT_REPOSITORY https://github.com/qcoro/qcoro.git
    GIT_TAG        v0.11.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(qcoro)
endif()
