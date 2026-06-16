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

if(GITGUI_BUILD_UI)
  # test_smoke.cpp (and future test_*.cpp) are #include-d into main.cpp so that
  # the custom QTest runner can instantiate every class in one binary.
  # We list them as HEADER_FILE_ONLY so AUTOMOC scans them for Q_OBJECT / moc
  # without compiling them as separate translation units.
  set(gitgui_ui_test_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_smoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_project_list_model.cpp
  )
  set_source_files_properties(${gitgui_ui_test_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

  add_executable(gitgui_ui_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/main.cpp
    ${gitgui_ui_test_sources}
  )
  set_target_properties(gitgui_ui_tests PROPERTIES AUTOMOC ON)
  target_include_directories(gitgui_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
  target_link_libraries(gitgui_ui_tests PRIVATE gitgui_ui Qt6::Widgets Qt6::Test)
  add_test(NAME gitgui_ui_tests COMMAND gitgui_ui_tests)
  set_tests_properties(gitgui_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
