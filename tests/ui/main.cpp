// HOW TO ADD A NEW UI TEST CLASS
// ================================
// Adding a new UI test requires TWO edits — missing either one compiles fine but
// silently skips the test:
//
//   Step 1 — Register the source in tests/CMakeLists.txt:
//     Add the new file to the `gittide_ui_test_sources` list (HEADER_FILE_ONLY)
//     so AUTOMOC scans it and Q_OBJECT macros are processed.
//
//   Step 2 — Wire it up in THIS file (tests/ui/main.cpp):
//     a) Add:  #include "test_<name>.cpp"
//     b) Add a QTest::qExec block in main():
//            MyNewTest t;
//            status |= QTest::qExec(&t, argc, argv);
//
// Both steps are mandatory. Omitting step 2 means the class compiles but is never
// executed — there will be no failure, no warning, just zero test runs for that class.

#include "test_async_repo.cpp"
#include "test_changed_files_list.cpp"
#include "test_branch_bar.cpp"
#include "test_branch_dialogs.cpp"
#include "test_changes_view.cpp"
#include "test_diff_view.cpp"
#include "test_history_model.cpp"
#include "test_history_view.cpp"
#include "test_main_window.cpp"
#include "test_project_controller.cpp"
#include "test_project_list_model.cpp"
#include "test_project_sidebar.cpp"
#include "test_qcoro_smoke.cpp"
#include "test_repo_controller.cpp"
#include "test_repo_list_model.cpp"
#include "test_session_store.cpp"
#include "test_smoke.cpp"
#include "test_theme.cpp"
#include "test_theme_manager.cpp"
#include "test_theme_style.cpp"
#include "test_window_manager.cpp"
#include "test_qml_theme.cpp"
#include "test_qml_shell.cpp"
#include "test_changed_files_model.cpp"
#include "test_diff_lines_model.cpp"
#include "test_repo_view_model.cpp"

#include <QApplication>
#include <QtTest/QtTest>
#include <git2.h>

#define RUN(T)                                                                                                                 \
    do                                                                                                                         \
    {                                                                                                                          \
        T t;                                                                                                                   \
        status |= QTest::qExec(&t, argc, argv);                                                                                 \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Hold one process-wide libgit2 reference for the whole run. The per-test repo
    // helpers each call git_libgit2_init()/git_libgit2_shutdown(); this anchor keeps
    // the refcount from hitting zero (and tearing down global state) between tests.
    Q_INIT_RESOURCE(icons);
    Q_INIT_RESOURCE(qml);
    git_libgit2_init();
    // Ignore the host's system/global/XDG git config so CI's Windows runner (which
    // sets core.autocrlf=true globally) cannot inject the CRLF filter into these
    // test repos — that filter ran on an AsyncRepo worker thread and crashed.
    for (int level : {GIT_CONFIG_LEVEL_PROGRAMDATA, GIT_CONFIG_LEVEL_SYSTEM, GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL})
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, "");

    int status = 0;
    RUN(TestChangedFilesList);
    RUN(TestUiSmoke);
    RUN(TestProjectListModel);
    RUN(TestRepoListModel);
    RUN(TestProjectController);
    RUN(TestRepoController);
    RUN(TestSessionStore);
    RUN(TestProjectSidebar);
    RUN(TestMainWindow);
    RUN(TestWindowManager);
    RUN(TestQCoroSmoke);
    RUN(TestAsyncRepo);
    RUN(TestBranchBar);
    RUN(TestBranchDialogs);
    RUN(TestDiffView);
    RUN(TestChangesView);
    RUN(TestHistoryModel);
    RUN(TestHistoryView);
    RUN(TestTheme);
    RUN(TestThemeStyle);
    RUN(TestThemeManager);
    RUN(TestQmlTheme);
    RUN(TestQmlShell);
    RUN(TestChangedFilesModel);
    RUN(TestDiffLinesModel);
    RUN(TestRepoViewModel);

    // Deliberately do not git_libgit2_shutdown(): AsyncRepo's QThreadPool workers
    // are joined only during static teardown, after main returns, so shutting down
    // here could free libgit2 state still referenced by an exiting worker. The
    // process is about to exit anyway, so leaving it initialised is harmless.
    return status;
}
