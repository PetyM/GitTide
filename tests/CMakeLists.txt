add_executable(gittide_core_tests
  test_smoke.cpp
  test_libgit2.cpp
  test_path_util.cpp
  test_git_error.cpp
  test_log.cpp
  test_temp_repo.cpp
  test_git_repo.cpp
  test_diff_engine.cpp
  test_git_repo_diff.cpp
  test_git_repo_stage.cpp
  test_git_repo_commit.cpp
  test_git_repo_identity.cpp
  test_project_store.cpp
  test_credentials_store.cpp
  test_build_patch.cpp
  test_git_repo_discard.cpp
  test_git_repo_stash.cpp
  test_git_repo_init_clone.cpp
  test_git_repo_log.cpp
  test_git_repo_commit_diff.cpp
  test_git_repo_branches.cpp
  test_git_repo_merge.cpp
  test_git_repo_rebase.cpp
  test_git_repo_interactive_rebase.cpp
  test_credential_select.cpp
  test_git_repo_checkout.cpp
  test_git_repo_reset.cpp
  test_git_repo_reword.cpp
  test_git_repo_undo.cpp
  test_git_repo_range_diff.cpp
  test_git_repo_submodules.cpp
  test_git_repo_submodule_merge.cpp
  test_git_repo_sync.cpp
  test_git_repo_watch.cpp
  test_graph_builder.cpp
  core/test_gitrepo_logallrefs.cpp
  core/test_submodule_detail.cpp
  core/test_commit_email.cpp
  core/test_git_repo_local_only.cpp
  support/temprepo.cpp
  support/git_config_isolation.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_identity_list_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_credential_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_secret_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_forge_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_avatar_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_list_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_project_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_async_repo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_watcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_async_merge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_async_rebase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_shell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_history.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_sync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_changed_files_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_diff_lines_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_view_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_branch_list_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_logging.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_auth_error.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_merge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repoviewmodel_merge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_difflinesmodel_conflict.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_merge_banner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_merge_entrypoints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_reword.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_undo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_discard_all.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_stash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_squash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_rebase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repoviewmodel_rebase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_banner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_entrypoints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_interactive_rebase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_todo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_banner_message.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_history_model_reflabels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_graph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_appcontrols.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_menu_bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_stash_list_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_stash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_syntaxhighlighter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_wheelscroller.cpp
  )
  set_source_files_properties(${gittide_ui_test_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

  add_executable(gittide_ui_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/support/temprepo.cpp
    ${gittide_ui_test_sources}
  )
  set_target_properties(gittide_ui_tests PROPERTIES AUTOMOC ON)
  target_include_directories(gittide_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui ${libgit2_SOURCE_DIR}/include)
  target_link_libraries(gittide_ui_tests PRIVATE
    gittide_ui Qt6::Gui Qt6::Test Qt6::Concurrent Qt6::Qml Qt6::Quick QCoro6::Core libgit2package)
  add_test(NAME gittide_ui_tests COMMAND gittide_ui_tests)
  set_tests_properties(gittide_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
