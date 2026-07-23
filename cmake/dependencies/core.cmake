# Core dependencies (always built): the only libraries core/ links.
# Both are consumed PRIVATE by gittide_core and built static (see the
# BUILD_SHARED_LIBS OFF in dependencies.cmake).
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
