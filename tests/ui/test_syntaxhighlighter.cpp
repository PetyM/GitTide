#include <QtTest>
#include <vector>

#include "gittide/ui/syntaxhighlighter.hpp"

using gittide::ui::SyntaxHighlighter;

class TestSyntaxHighlighter : public QObject
{
    Q_OBJECT
private slots:
    void cppKeywordGetsColourSpan()
    {
        SyntaxHighlighter hl;
        QVERIFY(hl.hasDefinition("main.cpp"));
        const std::vector<QString> out =
            hl.highlightLines("main.cpp", {QStringLiteral("int x = 1;")}, /*dark=*/true);
        QCOMPARE(out.size(), std::size_t(1));
        // Some token must be wrapped in a colour span.
        QVERIFY(out[0].contains(QStringLiteral("<span style=\"color:#")));
    }

    void escapesHtmlSpecials()
    {
        SyntaxHighlighter hl;
        const std::vector<QString> out =
            hl.highlightLines("main.cpp", {QStringLiteral("a < b && c > d;")}, true);
        QCOMPARE(out.size(), std::size_t(1));
        QVERIFY(out[0].contains(QStringLiteral("&lt;")));
        QVERIFY(out[0].contains(QStringLiteral("&gt;")));
        QVERIFY(out[0].contains(QStringLiteral("&amp;")));
        QVERIFY(!out[0].contains(QStringLiteral("< b")));   // raw '<' must be escaped
    }

    void unknownExtensionReturnsEmpty()
    {
        SyntaxHighlighter hl;
        QVERIFY(!hl.hasDefinition("notes.weirdext"));
        const std::vector<QString> out =
            hl.highlightLines("notes.weirdext", {QStringLiteral("anything")}, true);
        QVERIFY(out.empty());
    }

    void multiLineBlockCommentCarriesState()
    {
        // A block comment opened on line 1 must still be highlighted on line 2,
        // proving that KSyntaxHighlighting state propagates across lines.
        SyntaxHighlighter hl;
        QVERIFY(hl.hasDefinition("main.cpp"));
        const std::vector<QString> out = hl.highlightLines(
            "main.cpp",
            {QStringLiteral("/* open"), QStringLiteral("still comment */")},
            /*dark=*/true);
        QCOMPARE(out.size(), std::size_t(2));
        // Both lines must produce non-empty HTML.
        QVERIFY(!out[0].isEmpty());
        QVERIFY(!out[1].isEmpty());
        // Both lines must contain a colour span, confirming they were highlighted
        // (not left as plain escaped text with no spans at all).
        QVERIFY(out[0].contains(QStringLiteral("<span style=\"color:#")));
        QVERIFY(out[1].contains(QStringLiteral("<span style=\"color:#")));
    }
};

#include "test_syntaxhighlighter.moc"
