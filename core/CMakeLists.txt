add_library(gitgui_core STATIC)
target_sources(gitgui_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/LibGit2Context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/PathUtil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/GitError.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/GitRepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DiffEngine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectStore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/GraphBuilder.cpp
)
target_include_directories(gitgui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gitgui_core PUBLIC cxx_std_23)
# libgit2 and nlohmann/json are implementation details: no public header includes
# them (GitRepo.hpp only forward-declares struct git_repository), so they are
# PRIVATE and do not leak onto downstream consumers of gitgui_core.
# libgit2package does not export its headers via INTERFACE_INCLUDE_DIRECTORIES,
# so we pull the include path manually. FetchContent sets libgit2_SOURCE_DIR.
target_link_libraries(gitgui_core PRIVATE libgit2package nlohmann_json::nlohmann_json)
target_include_directories(gitgui_core PRIVATE ${libgit2_SOURCE_DIR}/include)
