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
#include <QSignalSpy>
#include <memory>

#include "gittide/diff.hpp"
#include "gittide/ui/diffselection.hpp"
#include "gittide/ui/difflinesmodel.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::DiffLinesModel;
using gittide::ui::DiffSelection;
using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

namespace
{
// One line, deliberately indented with a leading tab and carrying a run of
// internal spaces plus a "<" that syntax highlighting would HTML-escape.
// QTextDocument's default HTML parser strips leading whitespace and collapses
// internal space runs, which the "added two" sample above can't catch since
// it has neither.
gittide::DiffResult indentedLineDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "\tif (a  < b)";

    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 1;
    h.newStart = 1;
    h.newLines = 1;
    h.lines    = {ctx};

    gittide::DiffResult r;
    r.hunks = {h};
    return r;
}

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

// Captures qWarning/qCritical output during a message handler install, so a
// test can assert that an action produced *no* runtime warning — QML
// swallows an uncaught JS TypeError (e.g. calling a method on null) into a
// warning rather than crashing, so that's the only observable signal that
// onCopy/onCopyWithMarkers dereferenced a null `overlay.selection`.
QStringList* g_capturedMessages = nullptr;
void captureMessages(QtMsgType, const QMessageLogContext&, const QString& message)
{
    if (g_capturedMessages)
        g_capturedMessages->push_back(message);
}

// Builds the fake-list + DiffSelectionOverlay host every overlay test drives.
// The engine must already carry "theme" and "testSelection" context
// properties. Rows are 20px tall; each row's code column starts at x = 100.
//
// Row @p noCodeRow (default: none) gets no DiffCodeText at all — like a
// conflict header row, which has no code column for the overlay to select in.
//
// Row @p hiddenCodeRow (default: none) gets a real DiffCodeText that stays in
// the tree but sits behind a wrapper whose `visible` is false — modelling
// DiffView.qml's real conflict-start delegate, where the RowLayout holding
// the code column (and the Accept buttons' RowLayout underneath it) is
// `visible: model.lineKind !== "conflict-start"` while the row item itself
// (what itemAtIndex() returns) stays visible throughout.
//
// @p withScrollBar (default false) adds a 6px-wide stand-in scrollbar item
// docked to the host's right edge (x in [394, 400]) and wires it as the
// overlay's `scrollBar`, so a press there can be asserted as rejected. The
// stub carries a settable `size` property (default 0.5, mirroring
// ScrollBar.size < 1.0 — "content overflows, the handle is shown, the bar is
// interactive") so a test can flip it to 1.0 ("no overflow, handle hidden")
// and assert a press at the same spot is no longer swallowed.
//
// On a QML error, @p errorOut (if given) receives the component's error
// string and the returned pointer is null.
std::unique_ptr<QObject> buildOverlayHost(QQmlEngine& engine, int noCodeRow = -1, QString* errorOut = nullptr,
                                          int hiddenCodeRow = -1, bool withScrollBar = false)
{
    QQmlComponent comp(&engine);
    const QByteArray qml = QStringLiteral(R"QML(
        import QtQuick

        Item {
            id: host
            width: 400
            height: 60

            property var rows: ["@@ -1,1 +1,2 @@", "ctx one", "added two"]
            property int noCodeRow: %1
            property int hiddenCodeRow: %2

            Rectangle {
                id: scrollBarStub
                objectName: "scrollBarStub"
                property real size: 0.5
                x: host.width - width
                y: 0
                width: 6
                height: host.height
                color: "transparent"
            }

            Column {
                id: fakeList
                objectName: "fakeList"
                property real contentY: 0
                property real contentHeight: 60
                width: 400
                height: 60
                function indexAt(x, y) {
                    const row = Math.floor(y / 20)
                    return (row < 0 || row > 2) ? -1 : row
                }
                function itemAtIndex(i) { return repeater.itemAt(i) }
                Repeater {
                    id: repeater
                    model: 3
                    delegate: Item {
                        width: 400
                        height: 20
                        // Row noCodeRow gets no DiffCodeText at all, not just
                        // a hidden one — the overlay's hit-testing looks for
                        // an actual "diffCodeText" descendant.
                        //
                        // Row hiddenCodeRow gets one, but wrapped in an
                        // invisible parent — the item itself is always
                        // visible (it's what itemAtIndex() returns).
                        Item {
                            anchors.fill: parent
                            visible: index !== host.hiddenCodeRow
                            Loader {
                                active: index !== host.noCodeRow
                                sourceComponent: DiffCodeText {
                                    x: 100
                                    width: 300
                                    height: 20
                                    row: index
                                    selection: testSelection
                                    plainText: host.rows[index]
                                }
                            }
                        }
                    }
                }
            }

            DiffSelectionOverlay {
                objectName: "overlay"
                anchors.fill: parent
                list: fakeList
                selection: testSelection
                scrollBar: %3 ? scrollBarStub : null
            }
        }
    )QML")
                                    .arg(noCodeRow)
                                    .arg(hiddenCodeRow)
                                    .arg(withScrollBar ? QStringLiteral("true") : QStringLiteral("false"))
                                    .toUtf8();
    comp.setData(qml, QUrl(QStringLiteral("qrc:/qml/_test_diff_overlay_host.qml")));
    if (errorOut)
        *errorOut = comp.errorString();
    return std::unique_ptr<QObject>(comp.create());
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

        // Now the case that actually exercises HTML whitespace collapsing: a
        // leading tab and a run of two internal spaces, plus an escaped "<".
        // Selecting the whole row by its source length (12 columns) must
        // still yield exactly the source text back — if document positions
        // have drifted from source columns, this either throws (position
        // past the collapsed document's end) or returns a truncated/shifted
        // slice instead of the full 12-character line.
        DiffLinesModel indentedModel;
        indentedModel.setDiff(indentedLineDiff(), {}, false);
        DiffSelection indentedSelection;
        indentedSelection.setModel(&indentedModel);

        const QString indentedPlain = QStringLiteral("\tif (a  < b)");
        QCOMPARE(indentedPlain.size(), 12);
        obj->setProperty("row", 1);
        obj->setProperty("plainText", indentedPlain);
        obj->setProperty("html",
                          QStringLiteral("\t<span style=\"color:#ff0000;\">if</span> (a  &lt; b)"));
        obj->setProperty("selection", QVariant::fromValue(&indentedSelection));

        indentedSelection.begin(1, 0);
        indentedSelection.extendTo(1, indentedPlain.size());
        QCOMPARE(obj->property("selectedText").toString(), indentedPlain);
    }

    // A stand-in for the diff ListView. The overlay only ever asks a list for
    // indexAt / itemAtIndex / contentY / contentHeight / height, so a stub with
    // those members exercises it fully — and unlike a real ListView it works
    // headless, where delegates are never instantiated.
    void overlay_drag_builds_a_selection_across_rows()
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
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));

        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Press inside row 1 ("ctx one") at its very start, drag into row 2.
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 400), Q_ARG(QVariant, 45));
        QMetaObject::invokeMethod(overlay, "endDrag");

        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.startInRow(1), 0);
        QCOMPARE(selection.endInRow(2), 9); // dragged past the end of "added two"
    }

    // Shift+click extends from the current anchor via pressAt() — but the
    // paired release, if it falls into the ordinary click-count path (no
    // movement happened, so it looks like a plain click), calls
    // clickAt(..., 1), which clears what the press just extended. A
    // Shift+click is its own gesture and must survive its own release.
    void shift_click_extends_and_the_release_does_not_clear_it()
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
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // A drag establishes anchor = row 1, cursor = row 2 (mirrors the
        // test above); its release takes the "moved" branch, same as always.
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 400), Q_ARG(QVariant, 45));
        QMetaObject::invokeMethod(overlay, "handleRelease", Q_ARG(QVariant, 400), Q_ARG(QVariant, 45));
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.property("anchorRow").toInt(), 1);
        QCOMPARE(selection.startInRow(0), -1); // row 0 not part of the selection yet

        // Shift+click on row 0, with no movement before its release: pressAt
        // extends the cursor there (the anchor stays row 1); the release
        // must leave that extension alone rather than clearing it.
        QVariant pressed;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, pressed),
                                  Q_ARG(QVariant, 100), Q_ARG(QVariant, 10),
                                  Q_ARG(QVariant, int(Qt::ShiftModifier)));
        QVERIFY(pressed.toBool());
        QMetaObject::invokeMethod(overlay, "handleRelease", Q_ARG(QVariant, 100), Q_ARG(QVariant, 10));

        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.property("anchorRow").toInt(), 1); // anchor untouched
        QCOMPARE(selection.startInRow(0), 0); // the shift-click's extension held
    }

    void overlay_rejects_a_press_left_of_the_code_column()
    {
        // The checkbox, line-number gutter and sign columns must keep their own
        // click behaviour — a press there is not a text selection.
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        QVariant handled;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 40), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    void overlay_rejects_a_press_on_a_row_with_no_code_item()
    {
        // A conflict header row (or any row DiffView doesn't give a code
        // column) must not start a selection, even when the press falls
        // squarely inside where the code column would otherwise be.
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, /*noCodeRow=*/1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Row 1 ("ctx one") has no code item on this host; x=150 sits well
        // inside the code column, so a rejection here is about the missing
        // item, not the left-of-column case covered above.
        QVariant handled;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 150), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    // A conflict-start row's real delegate (DiffView.qml) always instantiates
    // its normal RowLayout — including the DiffCodeText — and merely sets
    // `visible: false` on it; the item stays in the tree. findCode() must
    // treat that the same as "no code item here", or a press on such a row
    // is read as a text-selection press and mouse.accepted swallows it
    // before it can reach the conflict Accept buttons that live in the same
    // spot on the sibling (visible) header layout.
    void overlay_rejects_a_press_on_a_row_whose_code_item_is_hidden_not_absent()
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
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, /*noCodeRow=*/-1, &error, /*hiddenCodeRow=*/1);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Row 1's DiffCodeText is present (unlike the noCodeRow case above)
        // but its wrapper is invisible. x=150 sits inside where its code
        // column would be.
        QVariant handled;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 150), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());
        QVERIFY(!selection.property("hasSelection").toBool());

        // A neighbouring visible row is unaffected — this isn't a global
        // regression, just row 1.
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 150), Q_ARG(QVariant, 45), Q_ARG(QVariant, 0));
        QVERIFY(handled.toBool());
    }

    // The overlay is anchors.fill'd above the whole ListView, including the
    // AppScrollBar docked to its right edge — a press on the handle must
    // reach the scrollbar, not be swallowed as a text-selection press.
    void overlay_rejects_a_press_on_the_scrollbar_but_accepts_beside_it()
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
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root =
            buildOverlayHost(engine, /*noCodeRow=*/-1, &error, /*hiddenCodeRow=*/-1, /*withScrollBar=*/true);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // The stub scrollbar docks to the host's right edge, x in [394, 400].
        QVariant handled;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 397), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());
        QVERIFY(!selection.property("hasSelection").toBool());

        // Just to its left, still inside the code column: a normal press.
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 380), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(handled.toBool());
    }

    // AppScrollBar's policy is ScrollBar.AsNeeded, and the Basic style's
    // ScrollBar is `visible: control.policy !== ScrollBar.AlwaysOff` — true
    // regardless of whether the content actually overflows. `visible` alone
    // is therefore not a correct proxy for "there is a real, clickable bar
    // here": on a diff short enough to need no scrolling, gating on it alone
    // would still refuse a press in the scrollbar's screen rect even though
    // there is nothing there to protect. What actually distinguishes an
    // interactive bar is its handle being shown, which AppScrollBar ties to
    // `size < 1.0` — so a press at the same spot must be rejected while the
    // stub bar reports itself "active" (size < 1.0, overflowing content) and
    // accepted once it reports "inactive" (size >= 1.0, nothing to scroll).
    void overlay_rejects_the_scrollbar_only_while_it_is_actually_interactive()
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
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root =
            buildOverlayHost(engine, /*noCodeRow=*/-1, &error, /*hiddenCodeRow=*/-1, /*withScrollBar=*/true);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QObject* scrollBarStub = root->findChild<QObject*>(QStringLiteral("scrollBarStub"));
        QVERIFY(overlay != nullptr);
        QVERIFY(scrollBarStub != nullptr);

        // Default size (0.5, < 1.0): the bar is "active" — a press at its
        // screen rect is rejected, same as overlay_rejects_a_press_on_the_
        // scrollbar_but_accepts_beside_it above.
        QVariant handled;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 397), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());

        // size 1.0: no overflow, the handle is hidden, `visible` alone (the
        // pre-fix check) would still say true — the same press must now be
        // accepted as an ordinary text-selection press.
        scrollBarStub->setProperty("size", 1.0);
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 397), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(handled.toBool());
    }

    void overlay_drag_through_a_row_with_no_code_item_extends_to_its_edge()
    {
        // moveTo()'s fallback for a code-less row picks an edge column based
        // on drag direction: the row's full length when the drag is moving
        // down through it, column 0 when moving back up.
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        // Row 1 ("ctx one") has no code item; row 0 (the hunk header) and
        // row 2 ("added two") do.
        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, /*noCodeRow=*/1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Dragging down from row 0 into the code-less row 1 extends through
        // it to its full length (7 == strlen("ctx one")).
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 10),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 150), Q_ARG(QVariant, 25));
        QMetaObject::invokeMethod(overlay, "endDrag");
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.startInRow(1), 0);
        QCOMPARE(selection.endInRow(1), 7);

        selection.clear();

        // Dragging up from row 2 into the code-less row 1: the cursor lands
        // at column 0 there, so once row 1 is the first row of the ordered
        // selection its start is 0 too — startInRow is what would catch a
        // reversed direction check; row 1 is still fully selected (endInRow
        // is its full length regardless of direction, since it isn't the
        // last row of the selection).
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 400), Q_ARG(QVariant, 45),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 150), Q_ARG(QVariant, 25));
        QMetaObject::invokeMethod(overlay, "endDrag");
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.startInRow(1), 0);
        QCOMPARE(selection.endInRow(1), 7);
    }

    void a_plain_click_clears_the_selection()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        selection.selectAll();
        QMetaObject::invokeMethod(overlay, "clickAt", Q_ARG(QVariant, 150), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 1));
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    void a_double_click_selects_the_word_and_a_triple_click_the_row()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Row 1 is "ctx one"; x = 100 is its first character.
        QMetaObject::invokeMethod(overlay, "clickAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 2));
        QCOMPARE(selection.copyText(false), QStringLiteral("ctx\n"));

        QMetaObject::invokeMethod(overlay, "clickAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 3));
        QCOMPARE(selection.copyText(false), QStringLiteral("ctx one\n"));
    }

    void ctrl_a_selects_all_and_ctrl_c_copies()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);
        QSignalSpy copySpy(overlay, SIGNAL(copyRequested(QString)));

        QVariant consumed;
        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_A)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier)));
        QVERIFY(consumed.toBool());
        QVERIFY(selection.property("hasSelection").toBool());

        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_C)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier)));
        QVERIFY(consumed.toBool());
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(copySpy.at(0).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\nctx one\nadded two\n"));

        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_C)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier | Qt::ShiftModifier)));
        QCOMPARE(copySpy.count(), 2);
        QCOMPARE(copySpy.at(1).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\n  ctx one\n+ added two\n"));
    }

    void keys_without_control_are_left_alone()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        QVariant consumed;
        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_A)), Q_ARG(QVariant, 0));
        QVERIFY(!consumed.toBool());
    }

    // `modifiers & Qt.ControlModifier` alone is true for Ctrl+Alt+A too, which
    // would steal a shortcut that belongs to something else (an Alt-chord).
    // handleKey must match the modifier set exactly.
    void ctrl_alt_a_is_left_alone()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        QVariant consumed;
        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_A)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier | Qt::AltModifier)));
        QVERIFY(!consumed.toBool());
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    // forceActiveFocus() has no QQuickWindow to promise activeFocus to under the
    // offscreen platform, but it does flip the item's own "focus" property — so
    // that is what pins down the fix that focus must follow only an accepted
    // press. Driven through handlePress(), the function onPressed delegates to,
    // since a QML "pressed" signal handler is not itself invokable from C++.
    void a_rejected_press_leaves_focus_alone_but_an_accepted_press_takes_it()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);
        QVERIFY(!overlay->property("focus").toBool());

        // Left of the code column: pressAt (and so handlePress) rejects it.
        QVariant handled;
        QMetaObject::invokeMethod(overlay, "handlePress", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 40), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());
        QVERIFY(!overlay->property("focus").toBool());

        // Inside the code column: handlePress accepts it and takes focus.
        QMetaObject::invokeMethod(overlay, "handlePress", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 100), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(handled.toBool());
        QVERIFY(overlay->property("focus").toBool());
    }

    // A drag is its own gesture. If the click counter survives it, a click
    // right after a same-row drag misreads as a double-click and selects a
    // word instead of clearing — see the sequence in the method name.
    void a_click_right_after_a_same_row_drag_does_not_count_as_a_double_click()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Click 1 on row 1 ("ctx one"): count 1, clears (no-op, nothing selected).
        QMetaObject::invokeMethod(overlay, "handleRelease", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25));

        // Drag on the same row: press, move (sets "moved"), release. The
        // release must reset the click counter, not just skip clickAt().
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 150), Q_ARG(QVariant, 25));
        QMetaObject::invokeMethod(overlay, "handleRelease", Q_ARG(QVariant, 150), Q_ARG(QVariant, 25));
        QVERIFY(selection.property("hasSelection").toBool()); // the drag itself selected text

        // Click 2, immediately after, same row: with a working reset this is
        // a fresh count-1 click (clears); with the bug it inherits the stale
        // count and lands as a double-click (selects a word instead).
        QMetaObject::invokeMethod(overlay, "handleRelease", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25));
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    void context_menu_disables_copy_without_a_selection()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffContextMenu.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> menu(comp.create());
        QVERIFY2(menu != nullptr, qPrintable(comp.errorString()));

        QObject* copyItem = menu->findChild<QObject*>(QStringLiteral("diffMenuCopy"));
        QObject* markersItem = menu->findChild<QObject*>(QStringLiteral("diffMenuCopyMarkers"));
        QObject* selectAllItem = menu->findChild<QObject*>(QStringLiteral("diffMenuSelectAll"));
        QVERIFY(copyItem != nullptr);
        QVERIFY(markersItem != nullptr);
        QVERIFY(selectAllItem != nullptr);

        QVERIFY(!copyItem->property("enabled").toBool());
        QVERIFY(!markersItem->property("enabled").toBool());
        QVERIFY(selectAllItem->property("enabled").toBool());

        menu->setProperty("hasSelection", true);
        QVERIFY(copyItem->property("enabled").toBool());
        QVERIFY(markersItem->property("enabled").toBool());
    }

    void overlay_menu_actions_copy_and_select_all()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QObject* menu = root->findChild<QObject*>(QStringLiteral("diffContextMenu"));
        QVERIFY(menu != nullptr);
        QSignalSpy copySpy(overlay, SIGNAL(copyRequested(QString)));

        QMetaObject::invokeMethod(menu, "selectAll");
        QVERIFY(selection.property("hasSelection").toBool());

        QMetaObject::invokeMethod(menu, "copy");
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(copySpy.at(0).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\nctx one\nadded two\n"));

        QMetaObject::invokeMethod(menu, "copyWithMarkers");
        QCOMPARE(copySpy.count(), 2);
        QCOMPARE(copySpy.at(1).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\n  ctx one\n+ added two\n"));
    }

    // Right-click never changes the selection — it acts on it. Drives the
    // overlay's real button-dispatch path (dispatchPress/dispatchRelease,
    // what onPressed/onReleased forward mouse.button to) rather than the
    // button-agnostic handlePress/handleRelease the other tests use, since
    // this is specifically about the right-button branch those don't cover.
    void right_click_press_and_release_does_not_touch_the_selection_or_click_counter()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        selection.selectAll();
        const QString before = selection.copyText(false);

        // Right-press inside row 1's ("ctx one") code column: accepted (so a
        // right-click can't leak through to whatever sits underneath), but
        // the selection must be exactly what it was before.
        QVariant handled;
        QMetaObject::invokeMethod(overlay, "dispatchPress", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, int(Qt::RightButton)), Q_ARG(QVariant, 100),
                                  Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(handled.toBool());
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.copyText(false), before);

        // The paired right-release must be a no-op too: not just leave the
        // selection alone, but not run the click-counting path at all.
        QMetaObject::invokeMethod(overlay, "dispatchRelease", Q_ARG(QVariant, int(Qt::RightButton)),
                                  Q_ARG(QVariant, 100), Q_ARG(QVariant, 25));
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.copyText(false), before);

        // A plain left click on the same row right after must still land as
        // a fresh single click (clears the selection), not a double (which
        // would select a word instead, leaving hasSelection true). Before
        // the fix, the unguarded right-release ran handleRelease() and
        // planted a bogus "click 1" in the counter, so this left click
        // misread as click 2 of a double-click.
        QMetaObject::invokeMethod(overlay, "dispatchRelease", Q_ARG(QVariant, int(Qt::LeftButton)),
                                  Q_ARG(QVariant, 100), Q_ARG(QVariant, 25));
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    void diff_view_declares_a_selection_bound_to_its_list_model()
    {
        // Registers DiffSelection under GitTide 1.0 — DiffView.qml `import
        // GitTide` needs it. This test worked without the call only because
        // some earlier test class in the same process happened to register
        // it first; call it explicitly here, the way test_qml_history.cpp
        // does for its own QML-instantiating tests.
        registerQmlTypes();

        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        // Cast the null explicitly — setContextProperty(QString, QObject*) is
        // ambiguous with the QVariant overload for a bare nullptr.
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"),
                                                 static_cast<QObject*>(nullptr));

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffView.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> view(comp.create());
        QVERIFY2(view != nullptr, qPrintable(comp.errorString()));

        QObject* selection = view->findChild<QObject*>(QStringLiteral("diffSelection"));
        QObject* overlay = view->findChild<QObject*>(QStringLiteral("diffSelectionOverlay"));
        QObject* list = view->findChild<QObject*>(QStringLiteral("diffList"));
        QVERIFY(selection != nullptr);
        QVERIFY(overlay != nullptr);
        QVERIFY(list != nullptr);

        // The overlay drives that selection over that list.
        QCOMPARE(overlay->property("selection").value<QObject*>(), selection);
        QCOMPARE(overlay->property("list").value<QObject*>(), list);

        // The selection follows whichever model the list is currently
        // showing (working diff, or the commit diff during stash preview) —
        // `diffSelection.model: diffList.model` in DiffView.qml. Reassign
        // the list's model and confirm the selection's model binding tracks
        // it, rather than only checking the two started out both null.
        DiffLinesModel altModel;
        altModel.setDiff(twoLineDiff(), {}, false);
        list->setProperty("model", QVariant::fromValue<QAbstractItemModel*>(&altModel));
        QCOMPARE(selection->property("model").value<QAbstractItemModel*>(),
                 static_cast<QAbstractItemModel*>(&altModel));
    }

    void commit_detail_declares_its_own_selection_over_the_commit_diff()
    {
        registerQmlTypes(); // see diff_view_declares_a_selection_bound_to_its_list_model()

        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"),
                                                 static_cast<QObject*>(nullptr));
        engine.rootContext()->setContextProperty(QStringLiteral("avatarService"),
                                                 static_cast<QObject*>(nullptr));

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/CommitDetail.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> detail(comp.create());
        QVERIFY2(detail != nullptr, qPrintable(comp.errorString()));

        QObject* selection = detail->findChild<QObject*>(QStringLiteral("commitDiffSelection"));
        QObject* overlay = detail->findChild<QObject*>(QStringLiteral("commitDiffSelectionOverlay"));
        QObject* list = detail->findChild<QObject*>(QStringLiteral("commitDiffList"));
        QVERIFY(selection != nullptr);
        QVERIFY(overlay != nullptr);
        QCOMPARE(overlay->property("selection").value<QObject*>(), selection);
        QCOMPARE(overlay->property("list").value<QObject*>(), list);

        // Same wiring assertion as DiffView above, for the read-only commit
        // diff's own selection/list pair.
        DiffLinesModel altModel;
        altModel.setDiff(twoLineDiff(), {}, false);
        list->setProperty("model", QVariant::fromValue<QAbstractItemModel*>(&altModel));
        QCOMPARE(selection->property("model").value<QAbstractItemModel*>(),
                 static_cast<QAbstractItemModel*>(&altModel));
    }

    // MouseArea's grab can be cancelled mid-drag (window deactivation,
    // another handler stealing it) without a matching onReleased. Without an
    // onCanceled handler, "dragging" stays true forever and the autoscroll
    // timer keeps stepping contentY and extending the selection on its own.
    void a_cancelled_grab_ends_the_drag()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 150), Q_ARG(QVariant, 45));
        QVERIFY(overlay->property("dragging").toBool());

        // MouseArea's "canceled" signal is an ordinary invokable in the
        // meta-object system — invoking it here emits it exactly as a real
        // grab loss would, running the onCanceled handler under test.
        QVERIFY(QMetaObject::invokeMethod(overlay, "canceled"));
        QVERIFY(!overlay->property("dragging").toBool());
    }

    // endDrag() (what onCanceled ran, above) only ever cleared `dragging` and
    // the autoscroll timer — it left `moved` at whatever the cancelled drag
    // had set it to. The very next plain click's release then finds `moved`
    // still true and takes handleRelease()'s "this click is its own gesture"
    // branch (the one a real drag's own release takes), which skips clickAt()
    // entirely — silently failing to clear the selection, exactly once.
    void a_cancelled_grab_leaves_no_gesture_state_for_the_next_click()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Start a drag on row 1 and move — same setup as
        // a_cancelled_grab_ends_the_drag, so `moved` is true when the grab is
        // cancelled instead of released normally.
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 150), Q_ARG(QVariant, 45));
        QVERIFY(overlay->property("moved").toBool());
        QVERIFY(selection.property("hasSelection").toBool()); // the drag itself selected text

        QVERIFY(QMetaObject::invokeMethod(overlay, "canceled"));
        QVERIFY(!overlay->property("dragging").toBool());

        // A plain click on the same row right after: with the fix this is a
        // fresh click (clears); with the bug it inherits `moved` == true from
        // the cancelled drag and handleRelease() silently returns without
        // ever calling clickAt().
        QMetaObject::invokeMethod(overlay, "handleRelease", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25));
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    // The overlay is topmost across the whole viewport, so a static I-beam
    // cursor would show over the checkboxes, gutter, sign column, Accept
    // buttons and scrollbar too. cursorShape is driven from canSelectAt(),
    // the same hit test pressAt() uses, via the hoverPos property that a
    // HoverHandler (real usage) or onPositionChanged (mid-drag) keeps
    // updated — set directly here since the offscreen platform delivers no
    // real hover events, so this cannot exercise the HoverHandler wiring
    // itself, only that the cursor binding still resolves through the same
    // hit test once hoverPos changes.
    void cursor_shape_follows_the_hover_hit_test()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QString error;
        std::unique_ptr<QObject> root =
            buildOverlayHost(engine, /*noCodeRow=*/-1, &error, /*hiddenCodeRow=*/-1, /*withScrollBar=*/true);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // hoverEnabled must stay false: a MouseArea with it true accepts
        // hover events and stops their delivery to items underneath (the
        // staging checkboxes, conflict Accept buttons, and the scrollbar's
        // own hover tint), even though this overlay covers their whole area.
        // The cursor still works via cursorShape (below), which a MouseArea
        // applies whenever the pointer is within its bounds regardless of
        // hoverEnabled — only hover *event delivery* is what hoverEnabled
        // would block.
        QVERIFY(!overlay->property("hoverEnabled").toBool());

        // Over the code column: I-beam.
        overlay->setProperty("hoverPos", QPointF(150, 25));
        QCOMPARE(overlay->property("cursorShape").toInt(), int(Qt::IBeamCursor));

        // Left of the code column (checkbox/gutter/sign area): arrow.
        overlay->setProperty("hoverPos", QPointF(40, 25));
        QCOMPARE(overlay->property("cursorShape").toInt(), int(Qt::ArrowCursor));

        // Over the scrollbar, at the right edge: arrow, not I-beam — the
        // scrollbar handle needs its own (default) cursor, not a text one.
        overlay->setProperty("hoverPos", QPointF(397, 25));
        QCOMPARE(overlay->property("cursorShape").toInt(), int(Qt::ArrowCursor));

        // Back over the code column: I-beam again.
        overlay->setProperty("hoverPos", QPointF(150, 25));
        QCOMPARE(overlay->property("cursorShape").toInt(), int(Qt::IBeamCursor));
    }

    // onCopy/onCopyWithMarkers must guard a null `overlay.selection` the same
    // way onSelectAll already did — without the guard they dereference it
    // directly and crash the moment a context-menu copy action fires while
    // no DiffSelection is wired up.
    void context_menu_copy_actions_guard_against_a_null_selection()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"),
                                                 static_cast<QObject*>(nullptr));

        QString error;
        std::unique_ptr<QObject> root = buildOverlayHost(engine, -1, &error);
        QVERIFY2(root != nullptr, qPrintable(error));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QObject* menu = root->findChild<QObject*>(QStringLiteral("diffContextMenu"));
        QVERIFY(overlay != nullptr);
        QVERIFY(menu != nullptr);
        QSignalSpy copySpy(overlay, SIGNAL(copyRequested(QString)));

        QStringList captured;
        g_capturedMessages = &captured;
        QtMessageHandler previous = qInstallMessageHandler(captureMessages);
        QMetaObject::invokeMethod(menu, "copy");
        QMetaObject::invokeMethod(menu, "copyWithMarkers");
        QMetaObject::invokeMethod(menu, "selectAll");
        qInstallMessageHandler(previous);
        g_capturedMessages = nullptr;

        // Unguarded, calling overlay.selection.copyText() on a null selection
        // raises an uncaught TypeError that QML swallows into a runtime
        // warning rather than a crash — so "no warning" is the guard's
        // observable effect here.
        QVERIFY2(captured.isEmpty(), qPrintable(captured.join(QStringLiteral("; "))));
        // None of the three should have emitted anything either, since there
        // is no selection to copy.
        QCOMPARE(copySpy.count(), 0);
    }

    // DiffCodeText's selFrom/selTo bindings read `selection.hasSelection`
    // inside their expression, and hasSelection's NOTIFY is
    // selectionChanged() — fired on every mutation, per the invariant
    // documented on that signal in diffselection.hpp. This is what makes
    // painting track a drag that only ever extends an already-active
    // selection, where hasSelection's *value* never flips: without that
    // guarantee (e.g. a future "only emit when the value changes" tidy-up)
    // this would silently stop updating while every other test stayed green.
    void extending_an_active_selection_keeps_the_painted_text_in_sync()
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
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        obj->setProperty("row", 2);
        obj->setProperty("plainText", QStringLiteral("added two"));
        obj->setProperty("selection", QVariant::fromValue(&selection));

        selection.begin(2, 0);
        selection.extendTo(2, 5); // "added"
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(obj->property("selectedText").toString(), QStringLiteral("added"));

        // Extend further without ever dropping the selection: hasSelection
        // is true both before and after, so this is exactly the case where
        // a value-gated NOTIFY would stop the binding from re-evaluating.
        selection.extendTo(2, 9); // "added two"
        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(obj->property("selectedText").toString(), QStringLiteral("added two"));
    }
};

#include "test_qml_diff_selection.moc"
