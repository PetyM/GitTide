add_executable(gitgui_core_tests
  test_smoke.cpp
  test_libgit2.cpp
  test_path_util.cpp
  test_git_error.cpp
  test_temp_repo.cpp
  support/TempRepo.cpp
)
target_link_libraries(gitgui_core_tests PRIVATE gitgui_core Catch2::Catch2WithMain)
target_include_directories(gitgui_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(Catch)
catch_discover_tests(gitgui_core_tests)
