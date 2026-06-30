// Tests for the title-bar menu bar components AppMenuBar.qml + MenuBarButton.qml
// (Plan 29, Task 7). The bar is not yet mounted into TitleBar (Task 8), so this
// test instantiates AppMenuBar.qml DIRECTLY via QQmlComponent — loaded by its QRC
// URL so same-dir App* types (AppMenu/AppMenuItem) resolve — with a theme context
// property and a stub repo/appSettings. It asserts the four menu-bar buttons exist
// and each owns its AppMenu.
//
// What is tested:
//   AppMenuBar instantiates and exposes File/Edit/View/Repository MenuBarButtons,
//   each with a non-null `menu`.

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QObject>
#include <memory>

#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

// ---------------------------------------------------------------------------
// Minimal repo stub exposing the enable-binding properties AppMenuBar reads, plus
// the per-repo invokables/counters (so Task 8 can reuse this shape). The bar's
// action signals are wired to the repo by the host (TitleBar) in Task 8; here we
// only need the properties to satisfy the enabled bindings.
// ---------------------------------------------------------------------------
class MenuBarRepoStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool repoOpen        READ repoOpen        CONSTANT)
    Q_PROPERTY(bool rebaseInProgress READ rebaseInProgress CONSTANT)
    Q_PROPERTY(bool mergeInProgress  READ mergeInProgress  CONSTANT)
    Q_PROPERTY(bool dirty           READ dirty           CONSTANT)
    Q_PROPERTY(bool stashAvailable  READ stashAvailable  CONSTANT)
public:
    explicit MenuBarRepoStub(QObject* parent = nullptr) : QObject(parent) {}

    bool repoOpen() const { return true; }
    bool rebaseInProgress() const { return false; }
    bool mergeInProgress() const { return false; }
    bool dirty() const { return true; }
    bool stashAvailable() const { return true; }

    Q_INVOKABLE void discardAll() { ++m_discardAllCalls; }
    Q_INVOKABLE void stashChanges() { ++m_stashCalls; }
    Q_INVOKABLE void popStash() { ++m_popCalls; }
    Q_INVOKABLE void openRepoFolder() { ++m_openFolderCalls; }
    Q_INVOKABLE void undoLastCommit() { ++m_undoCalls; }

    int m_discardAllCalls = 0, m_stashCalls = 0, m_popCalls = 0,
        m_openFolderCalls = 0, m_undoCalls = 0;
};

// ---------------------------------------------------------------------------

class TestQmlMenuBar : public QObject
{
    Q_OBJECT
private slots:

    void app_menu_bar_exposes_four_buttons_each_with_a_menu()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        MenuBarRepoStub repo;

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/AppMenuBar.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> bar(comp.create());
        QVERIFY2(bar != nullptr, qPrintable(comp.errorString()));

        bar->setProperty("repo", QVariant::fromValue(static_cast<QObject*>(&repo)));
        bar->setProperty("appSettings", QVariant());

        for (const QString& name : {QStringLiteral("menuBtnFile"),
                                    QStringLiteral("menuBtnEdit"),
                                    QStringLiteral("menuBtnView"),
                                    QStringLiteral("menuBtnRepository")})
        {
            QObject* btn = bar->findChild<QObject*>(name);
            QVERIFY2(btn != nullptr, qPrintable(name + " button not found"));
            QObject* menu = btn->property("menu").value<QObject*>();
            QVERIFY2(menu != nullptr, qPrintable(name + " has no menu"));
        }
    }
};

#include "test_qml_menu_bar.moc"
