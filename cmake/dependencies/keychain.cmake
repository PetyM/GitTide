# OS keychain (secret storage).
# QtKeychain wraps the platform secret store (macOS Keychain, Linux libsecret,
# Windows Credential Store) so HTTPS tokens / SSH passphrases never touch our own
# files. Prefer a system package; fall back to a pinned FetchContent build. Built
# static via the forced BUILD_SHARED_LIBS OFF in dependencies.cmake. Linux needs
# libsecret-1-dev.
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
