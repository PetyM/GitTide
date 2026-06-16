add_executable(gitgui_core_tests
  test_smoke.cpp
  test_libgit2.cpp
  test_path_util.cpp
  test_git_error.cpp
  test_temp_repo.cpp
  test_git_repo.cpp
  test_project_store.cpp
  support/TempRepo.cpp
)
# Tests use <git2.h> directly (test_libgit2.cpp, support/TempRepo.cpp), so the
# test target links libgit2 explicitly rather than relying on transitive exposure
# from gitgui_core (where libgit2 is a PRIVATE implementation detail).
target_link_libraries(gitgui_core_tests PRIVATE gitgui_core Catch2::Catch2WithMain libgit2package)
target_include_directories(gitgui_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${libgit2_SOURCE_DIR}/include)

include(Catch)
catch_discover_tests(gitgui_core_tests)
