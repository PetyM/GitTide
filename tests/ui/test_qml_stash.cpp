// Tests for StashPanel.qml — the collapsible stash-stack section in the Changes tab.
// Loads StashPanel.qml directly via its QRC URL with a stub repoVm and the real
// StashListModel. Asserts: visibility when empty, delegate appearance with entries,
// and that the Apply and Clear header buttons record their VM calls.

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <memory>

#include "gittide/ui/stashlistmodel.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::StashListModel;
using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;
using gittide::StashEntry;

// ---------------------------------------------------------------------------
// Minimal stub repoVm — exposes only the surface that StashPanel.qml reads.
// ---------------------------------------------------------------------------
class StashRepoStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool stashAvailable    READ stashAvailable    NOTIFY stashAvailableChanged)
    Q_PROPERTY(bool stashPreviewActive READ stashPreviewActive CONSTANT)
    Q_PROPERTY(QString stashPreviewLabel READ stashPreviewLabel CONSTANT)
    Q_PROPERTY(gittide::ui::StashListModel* stashes READ stashes CONSTANT)
public:
    explicit StashRepoStub(QObject* parent = nullptr)
        : QObject(parent)
        , m_stashes(new StashListModel(this))
    {}

    bool           stashAvailable()    const { return m_stashAvailable; }
    bool           stashPreviewActive() const { return false; }
    QString        stashPreviewLabel() const { return {}; }
    StashListModel* stashes()          const { return m_stashes; }

    // Helpers for test setup
    void setStashAvailable(bool v)
    {
        m_stashAvailable = v;
        emit stashAvailableChanged();
    }
    void loadEntries(const std::vector<StashEntry>& entries)
    {
        m_stashes->setEntries(entries);
    }

    // Call-recording invokables
    Q_INVOKABLE void previewStash(int row)  { m_previewStashCalls.append(row); }
    Q_INVOKABLE void exitStashPreview()     { ++m_exitPreviewCalls; }
    Q_INVOKABLE void applyStash(int row)    { m_applyStashCalls.append(row); }
    Q_INVOKABLE void popStashAt(int row)    { m_popStashCalls.append(row); }
    Q_INVOKABLE void dropStash(int row)     { m_dropStashCalls.append(row); }
    Q_INVOKABLE void clearStashes()         { ++m_clearStashesCalls; }

    QList<int> m_previewStashCalls;
    int        m_exitPreviewCalls  = 0;
    QList<int> m_applyStashCalls;
    QList<int> m_popStashCalls;
    QList<int> m_dropStashCalls;
    int        m_clearStashesCalls = 0;

signals:
    void stashAvailableChanged();

private:
    bool            m_stashAvailable = false;
    StashListModel* m_stashes;
};

// ---------------------------------------------------------------------------

class TestQmlStash : public QObject
{
    Q_OBJECT

    // Helper — create the engine with theme + stub wired, load StashPanel.qml.
    // width/height are set as initial properties so the ColumnLayout has explicit
    // dimensions from the first layout pass, allowing ListView delegates to
    // instantiate immediately without a bootstrap cycle.
    QObject* loadPanel(QQmlEngine& engine, QmlTheme& theme, StashRepoStub& stub,
                       int width = 0, int height = 0)
    {
        engine.rootContext()->setContextProperty(QStringLiteral("theme"),  &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);
        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/StashPanel.qml")));
        if (!comp.isReady()) {
            qWarning() << "StashPanel.qml load error:" << comp.errorString();
            return nullptr;
        }
        if (width > 0 || height > 0) {
            QVariantMap props;
            if (width  > 0) props[QStringLiteral("width")]  = width;
            if (height > 0) props[QStringLiteral("height")] = height;
            return comp.createWithInitialProperties(props);
        }
        return comp.create();
    }

private slots:

    // -----------------------------------------------------------------
    // 1. Panel is invisible when the stash stack is empty.
    // -----------------------------------------------------------------
    void stash_panel_hidden_when_stash_stack_empty()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        StashRepoStub stub; // stashAvailable=false, model empty

        QQmlEngine engine;
        std::unique_ptr<QObject> panel(loadPanel(engine, theme, stub, 400, 200));
        QVERIFY2(panel != nullptr, "StashPanel.qml failed to load");

        QCOMPARE(panel->property("visible").toBool(), false);
    }

    // -----------------------------------------------------------------
    // 2. Panel shows and stashList has one delegate when the model is populated.
    //    State is set before creation (static setup in direct-component tests).
    // -----------------------------------------------------------------
    void stash_panel_shows_with_one_delegate_after_model_gains_entry()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        StashRepoStub stub;
        // Populate before loading: the panel is visible from the first frame,
        // and the ListView has count=1 so it can instantiate its delegate.
        stub.loadEntries({{0, "WIP on main: abc", "aabbcc"}});
        stub.setStashAvailable(true);

        QQmlEngine engine;
        std::unique_ptr<QObject> panel(loadPanel(engine, theme, stub, 400, 200));
        QVERIFY2(panel != nullptr, "StashPanel.qml failed to load");

        // Panel must be visible because stashAvailable=true.
        QCOMPARE(panel->property("visible").toBool(), true);

        // ListView must report count=1 from the model.
        QObject* list = panel->findChild<QObject*>(QStringLiteral("stashList"));
        QVERIFY2(list != nullptr, "stashList not found inside stashPanel");
        QCOMPARE(list->property("count").toInt(), 1);
    }

    // -----------------------------------------------------------------
    // 3. Clicking a delegate's Apply button records applyStash(0).
    //
    //    Repeater delegate items are owned by the internal DelegateModel, not
    //    by their visual parent, so panel->findChild() cannot reach them.
    //    Instead, use Repeater.itemAt(0) to obtain the delegate item directly,
    //    then findChild inside that item (which IS its own QObject root).
    // -----------------------------------------------------------------
    void clicking_stash_apply_button_records_apply_stash()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        StashRepoStub stub;
        stub.loadEntries({{0, "WIP on main: abc", "aabbcc"}});
        stub.setStashAvailable(true);

        QQmlEngine engine;
        std::unique_ptr<QObject> panel(loadPanel(engine, theme, stub, 400, 200));
        QVERIFY2(panel != nullptr, "StashPanel.qml failed to load");
        QCOMPARE(panel->property("visible").toBool(), true);

        // Obtain the Repeater via its objectName.
        QObject* listObj = panel->findChild<QObject*>(QStringLiteral("stashList"));
        QVERIFY2(listObj != nullptr, "stashList Repeater not found in panel");

        // Repeater.itemAt(0) returns the first delegate item (QQuickItem*).
        QQuickItem* delegate = nullptr;
        bool ok = QMetaObject::invokeMethod(listObj, "itemAt",
                                            Qt::DirectConnection,
                                            Q_RETURN_ARG(QQuickItem*, delegate),
                                            Q_ARG(int, 0));
        QVERIFY2(ok && delegate != nullptr,
                 "Repeater.itemAt(0) returned null — delegate was not created");

        // The delegate is its own QObject root; its children include the buttons.
        QObject* btn = delegate->findChild<QObject*>(QStringLiteral("stashApplyButton"));
        QVERIFY2(btn != nullptr, "stashApplyButton not found inside delegate");

        QVERIFY(QMetaObject::invokeMethod(btn, "click"));
        QTest::qWait(50);

        QVERIFY2(!stub.m_applyStashCalls.isEmpty(), "applyStash was not called");
        QCOMPARE(stub.m_applyStashCalls.first(), 0);
    }

    // -----------------------------------------------------------------
    // 4. Clicking the header Clear button records clearStashes().
    // -----------------------------------------------------------------
    void clicking_stash_clear_button_records_clear_stashes()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        StashRepoStub stub;
        stub.loadEntries({{0, "WIP on main: abc", "aabbcc"}});
        stub.setStashAvailable(true);

        QQmlEngine engine;
        std::unique_ptr<QObject> panel(loadPanel(engine, theme, stub, 400, 200));
        QVERIFY2(panel != nullptr, "StashPanel.qml failed to load");

        QObject* btn = panel->findChild<QObject*>(QStringLiteral("stashClearButton"));
        QVERIFY2(btn != nullptr, "stashClearButton not found");
        QVERIFY(QMetaObject::invokeMethod(btn, "click"));
        QTest::qWait(50);

        QCOMPARE(stub.m_clearStashesCalls, 1);
    }
};

#include "test_qml_stash.moc"
