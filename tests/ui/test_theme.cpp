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
    void muted_text_meets_contrast_floor()
    {
        // text.muted was below the spec's 4.5:1 body-text floor on both themes;
        // re-tuned so history sub-lines, the commit medallion and path prefixes
        // read (design § Accessibility). Assert the corrected values.
        QCOMPARE(darkTheme().textMuted, QStringLiteral("#8E8E93"));
        QCOMPARE(lightTheme().textMuted, QStringLiteral("#6E6E73"));
    }
    void state_colors_are_per_theme()
    {
        // The git-state palette is now tuned per theme: the dark values are
        // bright-on-dark, the light values dark-on-light — a single shared hex
        // can't clear 4.5:1 on both grounds. Added/modified were near-invisible
        // (~2.3:1) on the light surface before this split.
        const Theme d = darkTheme();
        const Theme l = lightTheme();
        QCOMPARE(d.stateAdded, QStringLiteral("#3FB950"));
        QCOMPARE(l.stateAdded, QStringLiteral("#1A7F37"));
        QCOMPARE(l.stateModified, QStringLiteral("#9A6700"));
        QCOMPARE(l.stateDeleted, QStringLiteral("#CF222E"));
        QCOMPARE(l.stateConflict, QStringLiteral("#BC4C00"));
        QCOMPARE(l.stateIncoming, QStringLiteral("#0969DA"));
        // The two themes must genuinely differ for the tuned colours.
        QVERIFY(d.stateAdded != l.stateAdded);
        QVERIFY(d.stateModified != l.stateModified);
        QVERIFY(d.stateDeleted != l.stateDeleted);
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
