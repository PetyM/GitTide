#include "gittide/ui/windowmanager.hpp"

#include <QDir>
#include <QStandardPaths>
#include <filesystem>

#include "gittide/ui/mainwindow.hpp"
#include "gittide/ui/projectcontroller.hpp"

namespace gittide::ui {

WindowManager::WindowManager(QString configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                      : std::move(configDir))
{
}

WindowManager::~WindowManager()
{
    // Silence our own signals during teardown so that the destroyed-handler
    // lambda (which calls removeAll + emit windowCountChanged) does not notify
    // external observers while the manager is being destroyed.
    blockSignals(true);

    // Windows are not QObject-parented to us (they need QWidget* parent),
    // so we own them and must delete them explicitly.
    // Take a copy since deleting triggers the destroyed handler which calls removeAll.
    const QList<MainWindow*> toDelete = m_windows;
    for (auto* w : toDelete)
    {
        delete w;
    }
}

QString WindowManager::sessionFile() const
{
    return QDir(m_configDir).filePath(QStringLiteral("session.json"));
}

MainWindow* WindowManager::findWindowForProject(const QString& id) const
{
    for (auto* w : m_windows)
    {
        if (w->currentProjectId() == id)
            return w;
    }
    return nullptr;
}

MainWindow* WindowManager::createWindow(const QString& projectId)
{
    const std::filesystem::path projectsFile = std::filesystem::path(m_configDir.toStdString()) / "projects.json";
    MainWindow* w                            = new MainWindow(&m_store, projectsFile);
    w->showProject(projectId);
    m_windows.push_back(w);

    // "Open in new window" from inside a window always forces a new window.
    connect(w,
            &MainWindow::openInNewWindowRequested,
            this,
            [this](const QString& id)
            {
                openProject(id, /*forceNew=*/true);
            });

    // Drop from bookkeeping when destroyed (e.g. user closes the window).
    connect(w,
            &QObject::destroyed,
            this,
            [this, w]()
            {
                m_windows.removeAll(w);
                emit windowCountChanged(windowCount());
            });

    emit windowCountChanged(windowCount());
    return w;
}

MainWindow* WindowManager::openProject(const QString& projectId, bool forceNew)
{
    if (m_dedup && !forceNew)
    {
        if (MainWindow* existing = findWindowForProject(projectId))
        {
            existing->raise();
            existing->activateWindow();
            return existing;
        }
    }
    MainWindow* w = createWindow(projectId);
    w->show();
    return w;
}

void WindowManager::saveSession()
{
    SessionStore session;
    for (auto* w : m_windows)
    {
        session.windows.push_back(WindowSession{
            .projectId      = w->currentProjectId(),
            .geometry       = w->saveGeometry(),
            .lastActiveRepo = QString(), // populated in Plan 3 when repos are selectable
        });
    }
    QDir().mkpath(m_configDir);
    session.save(sessionFile());
}

void WindowManager::restoreSession()
{
    const SessionStore session = SessionStore::load(sessionFile());
    for (const auto& ws : session.windows)
    {
        MainWindow* w = createWindow(ws.projectId);
        if (!ws.geometry.isEmpty())
            w->restoreGeometry(ws.geometry);
        w->show();
    }
}

} // namespace gittide::ui
