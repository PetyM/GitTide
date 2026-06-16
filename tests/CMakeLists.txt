add_executable(gitgui_core_tests
  test_smoke.cpp
  test_libgit2.cpp
  test_path_util.cpp
)
target_link_libraries(gitgui_core_tests PRIVATE gitgui_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(gitgui_core_tests)
