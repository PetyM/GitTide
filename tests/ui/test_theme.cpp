#include <QtTest>
#include <QColor>

#include "gittide/ui/theme.hpp"

using namespace gittide::ui;

class TestTheme : public QObject
{
    Q_OBJECT
private slots:
    void dark_theme_has_brand_tokens()
    {
        const Theme t = darkTheme();
        QVERIFY(t.dark);
        QCOMPARE(t.surfaceBase, QStringLiteral("#1C1C1E"));
        QCOMPARE(t.accent, QStringLiteral("#42A5F5"));
        QCOMPARE(t.head, QStringLiteral("#E3F2FD"));
        QCOMPARE(t.textPrimary, QStringLiteral("#E4E4E6"));
    }
    void light_theme_has_brand_tokens()
    {
        const Theme t = lightTheme();
        QVERIFY(!t.dark);
        QCOMPARE(t.surfaceBase, QStringLiteral("#F5F5F5"));
        QCOMPARE(t.accent, QStringLiteral("#1976D2"));
        QCOMPARE(t.textPrimary, QStringLiteral("#212121"));
    }
    void state_colors_match_across_themes()
    {
        // Spec §2.3: state colors identical in both themes.
        QCOMPARE(darkTheme().stateAdded, lightTheme().stateAdded);
        QCOMPARE(darkTheme().stateDeleted, lightTheme().stateDeleted);
        QCOMPARE(darkTheme().stateAdded, QStringLiteral("#3FB950"));
    }
    void every_token_is_nonempty()
    {
        for (const Theme& t : {darkTheme(), lightTheme()})
        {
            for (const QString& tok : {t.surfaceBase,
                                       t.surfaceRaised,
                                       t.surfaceOverlay,
                                       t.border,
                                       t.textPrimary,
                                       t.textSecondary,
                                       t.textMuted,
                                       t.accent,
                                       t.accentHover,
                                       t.head,
                                       t.stateAdded,
                                       t.stateModified,
                                       t.stateDeleted,
                                       t.stateUntracked,
                                       t.stateConflict,
                                       t.shadow})
            {
                QVERIFY(!tok.isEmpty());
            }
        }
    }
    void shadow_is_translucent_in_both_themes()
    {
        // Elevation token (design §9): a translucent shadow colour, so a card's
        // drop shadow reads as depth, never a solid block.
        for (const Theme& t : {darkTheme(), lightTheme()})
        {
            const QColor c(t.shadow);
            QVERIFY(c.isValid());
            QVERIFY(c.alpha() < 255);
        }
    }
    void focus_border_token_exists()
    {
        const auto dark  = gittide::ui::darkTheme();
        const auto light = gittide::ui::lightTheme();
        // focusBorder is defined as accent in both themes.
        QCOMPARE(dark.focusBorder,  dark.accent);
        QCOMPARE(light.focusBorder, light.accent);
    }
};

#include "test_theme.moc"
