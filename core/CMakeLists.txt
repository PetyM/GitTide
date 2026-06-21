add_library(gittide_core STATIC)
target_sources(gittide_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/libgit2context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pathutil.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/giterror.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/gitrepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/credentialselect.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/diffengine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/projectstore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graphbuilder.cpp
)
target_include_directories(gittide_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gittide_core PUBLIC cxx_std_23)
# libgit2 and nlohmann/json are implementation details: no public header includes
# them (GitRepo.hpp only forward-declares struct git_repository), so they are
# PRIVATE and do not leak onto downstream consumers of gittide_core.
# libgit2package does not export its headers via INTERFACE_INCLUDE_DIRECTORIES,
# so we pull the include path manually. FetchContent sets libgit2_SOURCE_DIR.
target_link_libraries(gittide_core PRIVATE libgit2package nlohmann_json::nlohmann_json)
target_include_directories(gittide_core PRIVATE ${libgit2_SOURCE_DIR}/include)
