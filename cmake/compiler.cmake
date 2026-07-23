# C++ standard and per-compiler flags shared by every target in the build.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC defaults to the system code page for both the source and execution
# charset, which mangles the UTF-8 string/char literals this codebase relies on
# (e.g. non-ASCII test fixtures and em-dashes in test names). /utf-8 forces both
# to UTF-8, matching GCC/Clang behaviour on the other CI runners.
#
# /MP compiles the translation units within each target in parallel. Under the
# Visual Studio (MSBuild) generator `cmake --build --parallel` only parallelises
# across targets, so without /MP the many sources of a single target (notably the
# FetchContent-built libgit2) compile serially — the main reason the Windows CI
# build lagged Linux/macOS.
if(MSVC)
  add_compile_options(/utf-8 /MP)
endif()
