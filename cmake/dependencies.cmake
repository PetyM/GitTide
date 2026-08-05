# Aggregates every third-party dependency GitTide builds or finds. Each concern
# lives in its own file under cmake/dependencies/; this file sets the one global
# that must hold for all of them and includes each in dependency order.

include(FetchContent)

# Build FetchContent dependencies statically. libgit2 defaults BUILD_SHARED_LIBS
# to ON, which produces libgit2.dll on Windows; catch_discover_tests runs the test
# executable at build time to enumerate tests and cannot find the DLL (it is not on
# PATH then), failing the Windows build. Static libs sidestep DLL discovery entirely.
# Qt is an external find_package import and is unaffected by this flag.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Suppress every dependency's own test suite. Vendored projects (ECM, libgit2,
# Catch2, QCoro, KSyntaxHighlighting) register their tests with the same CTest
# instance as ours, so a bare `ctest --test-dir build` would report their
# failures as GitTide's. Our tests are gated by GITGUI_BUILD_TESTS, never by
# BUILD_TESTING, so turning this off costs us nothing. It must be set before the
# first FetchContent_MakeAvailable, hence here rather than in a per-dep file.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/dependencies/core.cmake)

if(GITGUI_BUILD_TESTS)
  include(${CMAKE_CURRENT_LIST_DIR}/dependencies/tests.cmake)
endif()

if(GITGUI_BUILD_UI)
  # Order matters: Qt before keychain (keychain builds against Qt6), and QCoro
  # before syntax_highlighting (QCoro must reach FetchContent_MakeAvailable before
  # ECM lands on CMAKE_MODULE_PATH — see qcoro.cmake / syntax_highlighting.cmake).
  include(${CMAKE_CURRENT_LIST_DIR}/dependencies/qt.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/dependencies/keychain.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/dependencies/qcoro.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/dependencies/syntax_highlighting.cmake)
endif()
