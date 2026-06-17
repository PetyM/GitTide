#pragma once
#include <QList>
#include <QObject>
#include <QString>

#include "gittide/projectstore.hpp"
#include "gittide/ui/sessionstore.hpp"

namespace gittide::ui {

class MainWindow;

// Process-wide owner of shared services and all MainWindows. Core services
// (ProjectStore) live here once; windows are views over shared state.
// "Active project" is per-window state held by each MainWindow's controller.
class WindowManager : public QObject
{
    Q_OBJECT
public:
    // configDir: where session.json lives. Defaults to the OS app-config dir.
    explicit WindowManager(QString configDir = {}, QObject* parent = nullptr);
    ~WindowManager() override;

    gittide::ProjectStore* store()
    {
        return &m_store;
    }

    void setDeduplicate(bool on)
    {
        m_dedup = on;
    }
    bool deduplicate() const
    {
        return m_dedup;
    }

    int windowCount() const
    {
        return static_cast<int>(m_windows.size());
    }

    // Open a window for the project. When dedup is on and forceNew is false,
    // raises an existing window for that project instead of opening a second.
    MainWindow* openProject(const QString& projectId, bool forceNew = false);

    // Recreate windows recorded in session.json.
    void restoreSession();
    // Persist the currently open windows (project id + geometry).
    void saveSession();

signals:
    void windowCountChanged(int count);

private:
    QString sessionFile() const;
    MainWindow* findWindowForProject(const QString& id) const;
    MainWindow* createWindow(const QString& projectId);

    gittide::ProjectStore m_store;
    QList<MainWindow*> m_windows;
    QString m_configDir;
    bool m_dedup = false;
};

} // namespace gittide::ui
