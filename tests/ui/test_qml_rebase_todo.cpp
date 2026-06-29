// Tests for RebaseTodoDialog.qml — the interactive-rebase todo editor.
// Loads the dialog directly via QQmlComponent (not via Main.qml) with minimal
// context stubs for `theme` and `repoVm`. Drives seed() / setActionForTest()
// and asserts the planValid property reacts correctly.

#pragma once
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>

#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

// ---------------------------------------------------------------------------
// Minimal repoVm stub — only needs to not crash when RebaseTodoDialog's
// Connections target it. The stub also exposes the signals the dialog listens
// to (rebaseTodoReady) and the invokable it calls (startInteractiveRebase,
// requestRebaseTodo) so QML doesn't log binding errors.
// ---------------------------------------------------------------------------
class RebaseTodoStub : public QObject
{
    Q_OBJECT
public:
    explicit RebaseTodoStub(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void requestRebaseTodo(const QString&) {}
    Q_INVOKABLE void startInteractiveRebase(const QString&, const QVariantList&, const QVariantList&)
    {
        emit startInteractiveRebaseCalled();
    }

signals:
    void rebaseTodoReady(const QString& base, const QVariantList& entries);
    void startInteractiveRebaseCalled();
};

// ---------------------------------------------------------------------------

class TestQmlRebaseTodo : public QObject
{
    Q_OBJECT

private slots:
    // -----------------------------------------------------------------
    // planValid is false when first row's action is "squash"
    // -----------------------------------------------------------------
    void start_disabled_when_first_row_is_squash()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RebaseTodoStub stub;

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/RebaseTodoDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QObject* dlg = comp.create();
        QVERIFY(dlg);

        // Seed two entries — both start as "pick".
        QMetaObject::invokeMethod(dlg, "seed",
            Q_ARG(QVariant, QStringLiteral("base0")),
            Q_ARG(QVariant, QVariant::fromValue(QVariantList{
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("a")},
                            {QStringLiteral("summary"), QStringLiteral("A")}},
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("b")},
                            {QStringLiteral("summary"), QStringLiteral("B")}}})));

        // After seed, planValid should be true (first row is "pick").
        QVERIFY(dlg->property("planValid").toBool());

        // Set row 0 action to "squash" → invalid: first row cannot be squash.
        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 0), Q_ARG(QVariant, QStringLiteral("squash")));
        QVERIFY(!dlg->property("planValid").toBool());

        delete dlg;
    }

    // -----------------------------------------------------------------
    // planValid is false when all rows are "drop"
    // -----------------------------------------------------------------
    void start_disabled_when_all_rows_drop()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RebaseTodoStub stub;

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/RebaseTodoDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QObject* dlg = comp.create();
        QVERIFY(dlg);

        QMetaObject::invokeMethod(dlg, "seed",
            Q_ARG(QVariant, QStringLiteral("base0")),
            Q_ARG(QVariant, QVariant::fromValue(QVariantList{
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("a")},
                            {QStringLiteral("summary"), QStringLiteral("A")}},
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("b")},
                            {QStringLiteral("summary"), QStringLiteral("B")}}})));

        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 0), Q_ARG(QVariant, QStringLiteral("drop")));
        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("drop")));
        QVERIFY(!dlg->property("planValid").toBool());

        delete dlg;
    }

    // -----------------------------------------------------------------
    // planValid is false when the first KEPT row is squash/fixup, even when
    // earlier rows are dropped (leading drops don't make squash legal — there
    // is no prior in-range commit to fold into).
    // -----------------------------------------------------------------
    void start_disabled_when_first_kept_row_is_squash()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RebaseTodoStub stub;

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/RebaseTodoDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QObject* dlg = comp.create();
        QVERIFY(dlg);

        QMetaObject::invokeMethod(dlg, "seed",
            Q_ARG(QVariant, QStringLiteral("base0")),
            Q_ARG(QVariant, QVariant::fromValue(QVariantList{
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("a")},
                            {QStringLiteral("summary"), QStringLiteral("A")}},
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("b")},
                            {QStringLiteral("summary"), QStringLiteral("B")}}})));

        // Drop the oldest row, squash the next → first kept row is squash → invalid.
        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 0), Q_ARG(QVariant, QStringLiteral("drop")));
        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("squash")));
        QVERIFY(!dlg->property("planValid").toBool());

        // Same with fixup.
        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("fixup")));
        QVERIFY(!dlg->property("planValid").toBool());

        // Flip the kept row to pick → now valid (a drop before a pick is fine).
        QMetaObject::invokeMethod(dlg, "setActionForTest",
            Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("pick")));
        QVERIFY(dlg->property("planValid").toBool());

        delete dlg;
    }

    // -----------------------------------------------------------------
    // Seeding with per-entry actions (squash flow) pre-fills the rows: oldest
    // pick, the rest squash → valid plan, and collectActions reflects it.
    // -----------------------------------------------------------------
    void seed_with_actions_prefills_squash()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RebaseTodoStub stub;

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/RebaseTodoDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QObject* dlg = comp.create();
        QVERIFY(dlg);

        // Oldest-first: a (pick), b (squash) — a per-row action seed.
        QMetaObject::invokeMethod(dlg, "seed",
            Q_ARG(QVariant, QStringLiteral("base0")),
            Q_ARG(QVariant, QVariant::fromValue(QVariantList{
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("a")},
                            {QStringLiteral("summary"), QStringLiteral("A")},
                            {QStringLiteral("action"), QStringLiteral("pick")}},
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("b")},
                            {QStringLiteral("summary"), QStringLiteral("B")},
                            {QStringLiteral("action"), QStringLiteral("squash")}}})));

        // First kept row is pick → valid.
        QVERIFY(dlg->property("planValid").toBool());

        // collectActions round-trips the seeded actions.
        QVariant ret;
        QMetaObject::invokeMethod(dlg, "collectActions", Q_RETURN_ARG(QVariant, ret));
        const QVariantList actions = ret.toList();
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions.at(0).toString(), QStringLiteral("pick"));
        QCOMPARE(actions.at(1).toString(), QStringLiteral("squash"));

        delete dlg;
    }

    // -----------------------------------------------------------------
    // planValid is true when first row is "pick" and not all are drop
    // -----------------------------------------------------------------
    void start_enabled_when_plan_valid()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RebaseTodoStub stub;

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/RebaseTodoDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QObject* dlg = comp.create();
        QVERIFY(dlg);

        QMetaObject::invokeMethod(dlg, "seed",
            Q_ARG(QVariant, QStringLiteral("base0")),
            Q_ARG(QVariant, QVariant::fromValue(QVariantList{
                QVariantMap{{QStringLiteral("oid"), QStringLiteral("a")},
                            {QStringLiteral("summary"), QStringLiteral("A")}}})));

        // Default action is "pick" → valid.
        QVERIFY(dlg->property("planValid").toBool());

        delete dlg;
    }
};

#include "test_qml_rebase_todo.moc"
