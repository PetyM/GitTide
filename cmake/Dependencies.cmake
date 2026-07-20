include(FetchContent)

# Build FetchContent dependencies statically. libgit2 defaults BUILD_SHARED_LIBS
# to ON, which produces libgit2.dll on Windows; catch_discover_tests runs the test
# executable at build time to enumerate tests and cannot find the DLL (it is not on
# PATH then), failing the Windows build. Static libs sidestep DLL discovery entirely.
# Qt is an external find_package import and is unaffected by this flag.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# --- Core dependencies (always built) ---
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)   # libgit2's own tests
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
# Network transports. USE_HTTPS=ON auto-selects the platform TLS backend
# (OpenSSL on Linux, SChannel on Windows, SecureTransport on macOS); only Linux
# needs an extra dev package (libssl-dev).
set(USE_HTTPS ON CACHE BOOL "" FORCE)

# USE_SSH=ON links libssh2 so the credential callback's ssh-agent / key auth
# works (libssh2-1-dev on Linux, `brew install libssh2` on macOS). Windows has no
# system libssh2 and is left OFF for now — Windows SSH (vcpkg vs USE_SSH=exec) is
# a deferred decision; see docs/decisions.md.
if(WIN32)
  set(USE_SSH OFF CACHE BOOL "" FORCE)
else()
  set(USE_SSH ON CACHE BOOL "" FORCE)
endif()
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
# Qt 6 comes from the system or aqtinstall, NEVER FetchContent. The UI is pure
# Qt Quick/QML (no QWidgets): Gui + Qml + Quick + QuickControls2 render it, Test
# for headless unit tests, Concurrent for off-main-thread git ops.
# QCoro adds co_await support over QFuture; it is built from source via FetchContent.
if(GITGUI_BUILD_UI)
  find_package(Qt6 REQUIRED COMPONENTS Gui Test Concurrent Svg Qml Quick QuickControls2 QuickTest Network)

  # --- OS keychain (secret storage) ---
  # QtKeychain wraps the platform secret store (macOS Keychain, Linux libsecret,
  # Windows Credential Store) so HTTPS tokens / SSH passphrases never touch our own
  # files. Prefer a system package; fall back to a pinned FetchContent build. Built
  # static via the forced BUILD_SHARED_LIBS OFF above. Linux needs libsecret-1-dev.
  find_package(Qt6Keychain QUIET)
  if(NOT Qt6Keychain_FOUND)
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)   # else QtKeychain looks for Qt5
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      qtkeychain
      GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
      GIT_TAG        v0.14.0
      GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(qtkeychain)
    # FetchContent exposes the raw target `qt6keychain`; find_package exposes
    # `Qt6Keychain::Qt6Keychain`. Alias so downstream links one spelling.
    if(TARGET qt6keychain AND NOT TARGET Qt6Keychain::Qt6Keychain)
      add_library(Qt6Keychain::Qt6Keychain ALIAS qt6keychain)
    endif()
  endif()

  # Trim QCoro to the modules we use; the rest pull in Qt components we don't link.
  # IMPORTANT: QCoro must be made available BEFORE ECM (Extra CMake Modules) is added
  # to CMAKE_MODULE_PATH. ECM 6.5.0 removed the INTERFACE keyword from
  # ecm_generate_pri_file; QCoro 0.11.0 uses that keyword and would fail if it found
  # ECM 6.5.0 on the module path instead of its own bundled ECM cmake files.
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

  # --- Syntax highlighting (KDE Frameworks) ---
  # ECM (KDE's Extra CMake Modules) must be on CMAKE_MODULE_PATH for
  # KSyntaxHighlighting's own CMakeLists.txt to find its helpers.
  # We add ECM AFTER QCoro is already configured (see comment above).
  FetchContent_Declare(
    ecm
    GIT_REPOSITORY https://invent.kde.org/frameworks/extra-cmake-modules.git
    GIT_TAG        v6.5.0
    GIT_SHALLOW    TRUE
  )
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)   # suppress ECM's and KSyntax's own tests
  FetchContent_MakeAvailable(ecm)
  # KSyntaxHighlighting does find_package(ECM CONFIG) and then *overwrites*
  # CMAKE_MODULE_PATH with ECM_MODULE_PATH, which points into ECM's INSTALL layout
  # (share/ECM/modules). FetchContent only builds ECM, never installs it, so those
  # module dirs don't exist and helper includes like ECMPoQmTools fail with
  # "Unknown CMake command". On dev machines a system ECM (e.g. Homebrew) hides the
  # problem; clean CI runners have none, so all three platforms break. Install our
  # pinned ECM into a build-local prefix so find_package(ECM) resolves a complete,
  # correct layout everywhere. ECM is script-only — the nested configure+install is
  # quick and compiles nothing. Guarded by the config file so we install only once.
  set(_ecm_prefix "${CMAKE_BINARY_DIR}/ecm-prefix")
  if(NOT EXISTS "${_ecm_prefix}/share/ECM/cmake/ECMConfig.cmake")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -S "${ecm_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}/ecm-prefix-build"
              "-DCMAKE_INSTALL_PREFIX=${_ecm_prefix}" -DBUILD_TESTING=OFF
      RESULT_VARIABLE _ecm_configure OUTPUT_QUIET)
    if(NOT _ecm_configure EQUAL 0)
      message(FATAL_ERROR "Failed to configure bundled ECM for install (exit ${_ecm_configure})")
    endif()
    execute_process(
      COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}/ecm-prefix-build"
      RESULT_VARIABLE _ecm_install OUTPUT_QUIET)
    if(NOT _ecm_install EQUAL 0)
      message(FATAL_ERROR "Failed to install bundled ECM (exit ${_ecm_install})")
    endif()
  endif()
  set(ECM_DIR "${_ecm_prefix}/share/ECM/cmake" CACHE PATH "" FORCE)

  FetchContent_Declare(
    KF6SyntaxHighlighting
    GIT_REPOSITORY https://invent.kde.org/frameworks/syntax-highlighting.git
    GIT_TAG        v6.5.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(KF6SyntaxHighlighting)
  # FetchContent creates the raw CMake target 'KF6SyntaxHighlighting'.
  # When the library is installed and found via find_package the target is
  # 'KF6::SyntaxHighlighting' (the EXPORT_NAME alias in the package config).
  # Create the alias unconditionally so downstream code can always spell
  # KF6::SyntaxHighlighting regardless of how the dep was acquired.
  if(TARGET KF6SyntaxHighlighting AND NOT TARGET KF6::SyntaxHighlighting)
    add_library(KF6::SyntaxHighlighting ALIAS KF6SyntaxHighlighting)
  endif()
endif()
