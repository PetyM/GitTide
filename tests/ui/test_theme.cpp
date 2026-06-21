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
        QCOMPARE(t.surfaceBase, QStringLiteral("#0B1623"));
        QCOMPARE(t.accent, QStringLiteral("#22D3EE"));
        QCOMPARE(t.head, QStringLiteral("#FFFFFF"));
        QCOMPARE(t.textPrimary, QStringLiteral("#C9D1D9"));
    }
    void light_theme_has_brand_tokens()
    {
        const Theme t = lightTheme();
        QVERIFY(!t.dark);
        QCOMPARE(t.surfaceBase, QStringLiteral("#EEF3F8"));
        QCOMPARE(t.accent, QStringLiteral("#0891B2"));
        QCOMPARE(t.textPrimary, QStringLiteral("#0B1623"));
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
};

#include "test_theme.moc"
