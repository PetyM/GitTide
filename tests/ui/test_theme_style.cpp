#include <QPalette>
#include <QtTest>

#include "gittide/ui/theme.hpp"
#include "gittide/ui/themestyle.hpp"

using namespace gittide::ui;

class TestThemeStyle : public QObject
{
    Q_OBJECT
private slots:
    void palette_uses_surface_and_text_tokens()
    {
        const auto t = gittide::ui::darkTheme();
        const QPalette p = gittide::ui::buildPalette(t);
        QCOMPARE(p.color(QPalette::Window).name().toUpper(), QString(t.surfaceBase).toUpper());
        QCOMPARE(p.color(QPalette::WindowText).name().toUpper(), QString(t.textPrimary).toUpper());
        QCOMPARE(p.color(QPalette::Base).name().toUpper(), QString(t.surfaceRaised).toUpper());
        QCOMPARE(p.color(QPalette::Highlight).name().toUpper(), QString(t.accent).toUpper());
    }
    void accent_stylesheet_references_accent_token()
    {
        const auto t = gittide::ui::darkTheme();
        const QString qss = gittide::ui::buildAccentStyleSheet(t);
        QVERIFY(qss.contains(t.accent));   // accent underline / focus uses the token
        QVERIFY(!qss.isEmpty());
    }
    void light_and_dark_produce_different_palettes()
    {
        const QPalette dark  = buildPalette(darkTheme());
        const QPalette light = buildPalette(lightTheme());
        QVERIFY(dark.color(QPalette::Window) != light.color(QPalette::Window));
    }
    void accent_stylesheet_contains_state_tokens()
    {
        const auto t = gittide::ui::darkTheme();
        const QString qss = gittide::ui::buildAccentStyleSheet(t);
        QVERIFY(qss.contains(t.stateAdded));
        QVERIFY(qss.contains(t.stateDeleted));
    }
    void accent_stylesheet_contains_objectname_selectors()
    {
        const QString qss = buildAccentStyleSheet(darkTheme());
        QVERIFY(qss.contains(QStringLiteral("#changedFilesList")));
        QVERIFY(qss.contains(QStringLiteral("#repoList")));
        QVERIFY(qss.contains(QStringLiteral("QTabBar::tab:selected")));
    }
};

#include "test_theme_style.moc"
