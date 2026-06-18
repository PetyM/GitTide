#include <QtTest>

#include "gittide/ui/theme.hpp"
#include "gittide/ui/themestyle.hpp"

using namespace gittide::ui;

class TestThemeStyle : public QObject
{
    Q_OBJECT
private slots:
    void stylesheet_is_nonempty_and_uses_tokens()
    {
        const Theme t     = darkTheme();
        const QString qss = buildStyleSheet(t);
        QVERIFY(!qss.isEmpty());
        QVERIFY(qss.contains(t.accent));                      // accent token used
        QVERIFY(qss.contains(t.surfaceBase));                 // base surface used
        QVERIFY(qss.contains(QStringLiteral("QPushButton"))); // styles buttons
    }
    void light_and_dark_produce_different_sheets()
    {
        QVERIFY(buildStyleSheet(darkTheme()) != buildStyleSheet(lightTheme()));
    }
    void primary_cta_is_styled_by_objectname()
    {
        // Empty-state CTAs are accent-filled; sheet must reference the id.
        const QString qss = buildStyleSheet(darkTheme());
        QVERIFY(qss.contains(QStringLiteral("#createProjectCta")));
    }
    void branch_bar_and_dialogs_are_styled_by_objectname()
    {
        const QString qss = buildStyleSheet(darkTheme());
        QVERIFY(qss.contains(QStringLiteral("#branchBar")));
        QVERIFY(qss.contains(QStringLiteral("#currentBranchButton")));
        QVERIFY(qss.contains(QStringLiteral("#newBranchDialog")));
    }
};

#include "test_theme_style.moc"
