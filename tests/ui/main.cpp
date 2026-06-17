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
#include "test_changes_view.cpp"
#include "test_dashboard_async.cpp"
#include "test_dashboard_model.cpp"
#include "test_diff_view.cpp"
#include "test_history_model.cpp"
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

#include <QApplication>
#include <QtTest/QtTest>
#include <git2.h>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Hold a process-wide libgit2 reference for the whole run. The per-test repo
    // helpers each call git_libgit2_init()/git_libgit2_shutdown(); without this
    // anchor the refcount can hit zero and tear down global state (incl. the
    // filter registry) while an AsyncRepo worker thread is mid-operation.
    git_libgit2_init();
    // Ignore the host's system/global/XDG git config so CI's Windows runner
    // (which sets core.autocrlf=true globally) cannot inject the CRLF filter into
    // these test repos — that filter ran on a worker thread and crashed.
    for (int level : {GIT_CONFIG_LEVEL_PROGRAMDATA, GIT_CONFIG_LEVEL_SYSTEM, GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL})
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, "");

    int status = 0;
    {
        TestUiSmoke t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestProjectListModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestRepoListModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestProjectController t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestRepoController t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestSessionStore t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestDashboardModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestProjectSidebar t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestMainWindow t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestWindowManager t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestQCoroSmoke t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestAsyncRepo t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestDashboardAsync t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestDiffView t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestChangesView t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestHistoryModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestTheme t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestThemeStyle t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestThemeManager t;
        status |= QTest::qExec(&t, argc, argv);
    }
    // Deliberately do NOT git_libgit2_shutdown(): AsyncRepo runs git operations
    // on the global QThreadPool, whose worker threads stay alive (idle) past the
    // end of main and are only joined during static teardown. Shutting libgit2
    // down here would free its global/TLS state while those threads still exist;
    // their later exit would touch freed state and crash (fatal on Windows). The
    // process is about to exit, so leaving libgit2 initialized is harmless.
    return status;
}
