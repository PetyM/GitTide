#include "gittide/ui/repowatcher.hpp"

#include <QDir>
#include <QFileSystemWatcher>
#include <QLatin1Char>
#include <QStringList>
#include <QTimer>

#include "gittide/log.hpp"

namespace gittide::ui {

RepoWatcher::RepoWatcher(int debounceMs, QObject* parent)
    : QObject(parent)
    , m_fsw(new QFileSystemWatcher(this))
    , m_timer(new QTimer(this))
    , m_debounceMs(debounceMs)
{
    m_timer->setSingleShot(true);
    connect(m_fsw, &QFileSystemWatcher::directoryChanged, this, &RepoWatcher::onPathChanged);
    connect(m_fsw, &QFileSystemWatcher::fileChanged, this, &RepoWatcher::onPathChanged);
    connect(m_timer, &QTimer::timeout, this, &RepoWatcher::onDebounceElapsed);
}

RepoWatcher::~RepoWatcher() = default;

void RepoWatcher::watch(const gittide::WatchTargets& targets)
{
    clear();
    m_gitDirPrefix = QDir::cleanPath(QString::fromStdString(targets.gitDir.generic_string()));

    QStringList paths;
    paths.reserve(static_cast<int>(targets.dirs.size()));
    for (const auto& d : targets.dirs)
        paths << QDir::cleanPath(QString::fromStdString(d.generic_string()));

    if (!paths.isEmpty())
    {
        const QStringList failed = m_fsw->addPaths(paths);
        if (!failed.isEmpty())
            logf(LogLevel::Debug, logcat::ASYNC, "RepoWatcher: {} of {} paths could not be watched",
                 failed.size(), paths.size());
    }
}

void RepoWatcher::clear()
{
    const QStringList all = m_fsw->directories() + m_fsw->files();
    if (!all.isEmpty())
        m_fsw->removePaths(all);
    m_timer->stop();
    m_pendingWork = false;
    m_pendingGit  = false;
}

void RepoWatcher::mute()
{
    m_muted = true;
}

void RepoWatcher::unmute()
{
    // Clear the mute one debounce window later: the filesystem events from the
    // just-finished mutation arrive slightly after the write, so dropping them
    // for one more window avoids a redundant refresh right after our own cascade.
    QTimer::singleShot(m_debounceMs, this, [this]() { m_muted = false; });
}

void RepoWatcher::onPathChanged(const QString& path)
{
    if (m_muted)
        return;
    const QString clean = QDir::cleanPath(path);
    if (!m_gitDirPrefix.isEmpty()
        && (clean == m_gitDirPrefix || clean.startsWith(m_gitDirPrefix + QLatin1Char('/'))))
        m_pendingGit = true;
    else
        m_pendingWork = true;
    m_timer->start(m_debounceMs);
}

void RepoWatcher::onDebounceElapsed()
{
    const bool git  = m_pendingGit;
    const bool work = m_pendingWork;
    m_pendingGit    = false;
    m_pendingWork   = false;
    if (git)
        emit gitDirChanged();
    if (work)
        emit worktreeChanged();
}

} // namespace gittide::ui
