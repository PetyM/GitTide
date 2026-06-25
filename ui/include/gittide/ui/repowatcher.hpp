#pragma once
#include <QObject>
#include <QString>

#include "gittide/watch.hpp"

class QFileSystemWatcher;
class QTimer;

namespace gittide::ui {

/// Watches a repository's working-tree directories and its git dir, emitting a
/// debounced, classified change signal so the UI can refresh itself (D35).
///
/// Lives in `ui/` because `QFileSystemWatcher` / `QTimer` are Qt. It knows nothing
/// about libgit2: it is handed a gittide::WatchTargets (computed by `core/`) and
/// watches the directories listed there. A burst of filesystem events is coalesced
/// over the debounce window; on fire it emits worktreeChanged() if any working-tree
/// directory changed and gitDirChanged() if any path under the git dir changed.
///
/// mute()/unmute() suppress emissions while the owning controller performs its own
/// mutations, so self-induced writes do not trigger a redundant refresh. Refreshes
/// are read-only, so there is no feedback loop to break beyond this convenience.
class RepoWatcher : public QObject
{
    Q_OBJECT
public:
    /// @param debounceMs how long to coalesce a burst before emitting. Injectable
    /// so tests run fast and deterministically.
    explicit RepoWatcher(int debounceMs = 300, QObject* parent = nullptr);
    ~RepoWatcher() override;

    /// Replace the watch set with the directories in @p targets.
    void watch(const gittide::WatchTargets& targets);
    /// Stop watching everything and drop any pending batch.
    void clear();

    /// Drop filesystem events (call around the controller's own mutations).
    void mute();
    /// Resume watching. Trailing events within one debounce window are still
    /// dropped, so a just-completed mutation does not refire the watcher.
    void unmute();

signals:
    void worktreeChanged(); ///< only working-tree directories changed → status scope
    void gitDirChanged();   ///< a path under the git dir changed → full-cascade scope

private slots:
    void onPathChanged(const QString& path);
    void onDebounceElapsed();

private:
    QFileSystemWatcher* m_fsw;
    QTimer*             m_timer;
    QString             m_gitDirPrefix;       ///< cleaned git-dir path (no trailing slash)
    int                 m_debounceMs;
    bool                m_muted       = false;
    bool                m_pendingWork = false;
    bool                m_pendingGit  = false;
};

} // namespace gittide::ui
