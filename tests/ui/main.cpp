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
#include <cstdio>
#include <cstdlib>
#include <git2.h>

// Direct, unbuffered stderr marker that survives an abnormal exit, so CI logs
// show exactly which stage/test was running when the process died.
#define MARK(msg)                                                                                                              \
    do                                                                                                                         \
    {                                                                                                                          \
        std::fprintf(stderr, "[mark] %s\n", msg);                                                                             \
        std::fflush(stderr);                                                                                                   \
    } while (0)
#define RUN(T)                                                                                                                 \
    do                                                                                                                         \
    {                                                                                                                          \
        MARK("run " #T);                                                                                                       \
        T t;                                                                                                                   \
        status |= QTest::qExec(&t, argc, argv);                                                                                \
    } while (0)

int main(int argc, char** argv)
{
    // Unbuffer stdout/stderr so QTest output reaches ctest's capture pipe
    // immediately. With default full buffering on a pipe, an abnormal exit would
    // discard the entire buffer, leaving an empty "***Failed" with no diagnostics.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    MARK("main entered");

    QApplication app(argc, argv);
    MARK("QApplication constructed");

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
    MARK("libgit2 initialised");

    int status = 0;
    RUN(TestUiSmoke);
    RUN(TestProjectListModel);
    RUN(TestRepoListModel);
    RUN(TestProjectController);
    RUN(TestRepoController);
    RUN(TestSessionStore);
    RUN(TestDashboardModel);
    RUN(TestProjectSidebar);
    RUN(TestMainWindow);
    RUN(TestWindowManager);
    RUN(TestQCoroSmoke);
    RUN(TestAsyncRepo);
    RUN(TestDashboardAsync);
    RUN(TestDiffView);
    RUN(TestChangesView);
    RUN(TestHistoryModel);
    RUN(TestTheme);
    RUN(TestThemeStyle);
    RUN(TestThemeManager);
    MARK("all tests done");
    // Hard-exit instead of returning, to skip global/static destructors. AsyncRepo
    // runs git operations on the global QThreadPool, whose worker threads are
    // joined only during static teardown; on Windows that teardown touches
    // libgit2 thread-local state in an order that crashes, turning an all-green
    // run into an abnormal exit (and a ctest failure). Tests clean up their own
    // temp dirs as they go, so skipping destructors is safe here. Flush first so
    // no buffered output is lost (belt-and-braces with the unbuffered streams).
    std::fflush(nullptr);
    std::_Exit(status);
}
