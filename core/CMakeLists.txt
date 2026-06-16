add_library(gitgui_core STATIC)
target_sources(gitgui_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/LibGit2Context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/PathUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/GitError.cpp
)
target_include_directories(gitgui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gitgui_core PUBLIC cxx_std_23)
# libgit2package is a static lib target; its headers are not exported via
# INTERFACE_INCLUDE_DIRECTORIES, so we must pull the include path manually.
# FetchContent sets libgit2_SOURCE_DIR after FetchContent_MakeAvailable().
target_link_libraries(gitgui_core PUBLIC libgit2package)
target_include_directories(gitgui_core PUBLIC ${libgit2_SOURCE_DIR}/include)
