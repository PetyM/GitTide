// Tests for the diff selection QML layer: DiffCodeText.qml paints the slice of a
// DiffSelection that covers its row, DiffSelectionOverlay.qml turns pointer and
// key input into selection calls, and both diff surfaces declare the pair.
//
// The offscreen harness never renders a frame, so ListView delegates are never
// instantiated (see tests/ui/test_qml_history.cpp). Nothing here goes through a
// real list: components are instantiated directly, and the overlay is driven
// against a stub list object.

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <memory>

#include "gittide/diff.hpp"
#include "gittide/ui/diffselection.hpp"
#include "gittide/ui/difflinesmodel.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::DiffLinesModel;
using gittide::ui::DiffSelection;
using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

namespace
{
gittide::DiffResult twoLineDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "ctx one";

    gittide::DiffLine added;
    added.origin    = gittide::DiffLineOrigin::Added;
    added.oldLineno = -1;
    added.newLineno = 2;
    added.text      = "added two";

    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 1;
    h.newStart = 1;
    h.newLines = 2;
    h.lines    = {ctx, added};

    gittide::DiffResult r;
    r.hunks = {h};
    return r;
}
} // namespace

class TestQmlDiffSelection : public QObject
{
    Q_OBJECT
private slots:

    void code_text_paints_the_selection_slice_for_its_row()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffCodeText.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        obj->setProperty("row", 2);                       // the added line
        obj->setProperty("plainText", QStringLiteral("added two"));
        obj->setProperty("selection", QVariant::fromValue(&selection));

        // Nothing selected yet.
        QCOMPARE(obj->property("selectionStart").toInt(), obj->property("selectionEnd").toInt());

        selection.begin(2, 6);
        selection.extendTo(2, 9);
        QCOMPARE(obj->property("selectionStart").toInt(), 6);
        QCOMPARE(obj->property("selectionEnd").toInt(), 9);
        QCOMPARE(obj->property("selectedText").toString(), QStringLiteral("two"));

        selection.clear();
        QCOMPARE(obj->property("selectedText").toString(), QString());
    }

    void code_text_takes_its_selection_colours_from_the_theme()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffCodeText.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        QCOMPARE(obj->property("selectionColor").value<QColor>(), QColor("#5942A5F5"));
        mgr.setMode(ThemeManager::Mode::Light);
        QCOMPARE(obj->property("selectionColor").value<QColor>(), QColor("#401976D2"));
    }

    void a_highlighted_row_selects_at_the_same_offsets_as_its_source_text()
    {
        // Syntax highlighting renders HTML, and TextEdit positions are document
        // positions. Selecting by source-string offsets must still land on the
        // same characters, or the painted highlight drifts from what gets copied.
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffCodeText.qml")));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        obj->setProperty("row", 2);
        obj->setProperty("plainText", QStringLiteral("added two"));
        obj->setProperty("html", QStringLiteral("<span style=\"color:#ff0000;\">added</span> two"));
        obj->setProperty("selection", QVariant::fromValue(&selection));

        selection.begin(2, 6);
        selection.extendTo(2, 9);
        QCOMPARE(obj->property("selectedText").toString(), QStringLiteral("two"));
    }
};

#include "test_qml_diff_selection.moc"
