// HOW TO ADD A NEW UI TEST CLASS
// ================================
// Adding a new UI test requires TWO edits — missing either one compiles fine but
// silently skips the test:
//
//   Step 1 — Register the source in tests/CMakeLists.txt:
//     Add the new file to the `gitgui_ui_test_sources` list (HEADER_FILE_ONLY)
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

#include <QApplication>
#include <QtTest/QtTest>

#include "test_smoke.cpp"
#include "test_project_list_model.cpp"
#include "test_repo_list_model.cpp"
#include "test_project_controller.cpp"
#include "test_repo_controller.cpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
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
    return status;
}
