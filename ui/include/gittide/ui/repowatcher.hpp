#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

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

    /// Replace the watch set with the directories in @p targets (plus the active
    /// file, if any). Incremental: only paths that left the set are removed and
    /// only new ones added, and neither the debounce timer nor the pending batch
    /// is touched. The controller re-arms after every refresh cascade, so a
    /// remove-all/add-all rebuild would both blind the watcher for an instant and
    /// discard events that arrived during the refresh.
    void watch(const gittide::WatchTargets& targets);
    /// Stop watching everything and drop any pending batch. For repo close — not
    /// part of re-arming (see watch()).
    void clear();

    /// Additionally watch a single file by absolute path (the diff currently on
    /// screen). Directory watches miss in-place content edits of an existing file
    /// — a per-file watch catches them so the open diff refreshes. Empty clears it.
    /// Survives watch() re-arming. A change is reported as worktreeChanged().
    void setActiveFile(const QString& absPath);

    /// Drop filesystem events (call around the controller's own mutations).
    /// Reference-counted: mute() nests, so an inner guard releasing while an
    /// outer cascade still holds one does not resume watching.
    void mute();
    /// Release one mute reference. Watching resumes only when the count reaches
    /// zero, and then one debounce window later — trailing events from the
    /// just-completed mutation are still dropped, so it does not refire the
    /// watcher. A later mute() taken before that deferred release fires
    /// invalidates it, so the new mute is never cut short.
    void unmute();

signals:
    void worktreeChanged(); ///< only working-tree directories changed → status scope
    void gitDirChanged();   ///< a path under the git dir changed → full-cascade scope

private slots:
    void onPathChanged(const QString& path);
    void onDebounceElapsed();

private:
    /// Bring the QFileSystemWatcher's path set to exactly @p desired by removing
    /// what left it and adding what joined it. Never a full rebuild.
    void applyWatchSet(const QStringList& desired);

    QFileSystemWatcher* m_fsw;
    QTimer*             m_timer;
    QString             m_gitDirPrefix;       ///< cleaned git-dir path (no trailing slash)
    QString             m_activeFile;         ///< cleaned abs path of the on-screen file, or empty
    int                 m_debounceMs;
    bool                m_muted       = false; ///< true while events are dropped
    int                 m_muteDepth   = 0;     ///< outstanding mute() calls
    quint64             m_unmuteGen   = 0;     ///< invalidates a deferred release superseded by a new mute()
    bool                m_pendingWork = false;
    bool                m_pendingGit  = false;
};

} // namespace gittide::ui
