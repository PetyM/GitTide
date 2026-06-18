add_executable(gittide_core_tests
  test_smoke.cpp
  test_libgit2.cpp
  test_path_util.cpp
  test_git_error.cpp
  test_temp_repo.cpp
  test_git_repo.cpp
  test_diff_engine.cpp
  test_git_repo_diff.cpp
  test_git_repo_stage.cpp
  test_git_repo_commit.cpp
  test_project_store.cpp
  test_build_patch.cpp
  test_git_repo_discard.cpp
  test_git_repo_init_clone.cpp
  test_git_repo_log.cpp
  test_git_repo_commit_diff.cpp
  test_git_repo_branches.cpp
  test_git_repo_checkout.cpp
  test_git_repo_reset.cpp
  test_graph_builder.cpp
  support/temprepo.cpp
)
# Tests use <git2.h> directly (test_libgit2.cpp, support/temprepo.cpp), so the
# test target links libgit2 explicitly rather than relying on transitive exposure
# from gittide_core (where libgit2 is a PRIVATE implementation detail).
target_link_libraries(gittide_core_tests PRIVATE gittide_core Catch2::Catch2WithMain libgit2package)
target_include_directories(gittide_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${libgit2_SOURCE_DIR}/include)

include(Catch)
catch_discover_tests(gittide_core_tests)

if(GITGUI_BUILD_UI)
  # test_smoke.cpp (and future test_*.cpp) are #include-d into main.cpp so that
  # the custom QTest runner can instantiate every class in one binary.
  # We list them as HEADER_FILE_ONLY so AUTOMOC scans them for Q_OBJECT / moc
  # without compiling them as separate translation units.
  set(gittide_ui_test_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_smoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qcoro_smoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_project_list_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_list_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_project_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_session_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_dashboard_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_project_sidebar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_main_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_window_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_async_repo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_dashboard_async.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_diff_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_changes_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_history_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme_style.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_branch_bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_branch_dialogs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_history_view.cpp
  )
  set_source_files_properties(${gittide_ui_test_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

  add_executable(gittide_ui_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/main.cpp
    ${gittide_ui_test_sources}
  )
  set_target_properties(gittide_ui_tests PROPERTIES AUTOMOC ON)
  target_include_directories(gittide_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui ${libgit2_SOURCE_DIR}/include)
  target_link_libraries(gittide_ui_tests PRIVATE
    gittide_ui Qt6::Widgets Qt6::Test Qt6::Concurrent QCoro6::Core libgit2package)
  add_test(NAME gittide_ui_tests COMMAND gittide_ui_tests)
  set_tests_properties(gittide_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
