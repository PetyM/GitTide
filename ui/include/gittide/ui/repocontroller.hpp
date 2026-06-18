#pragma once
#include <QObject>
#include <QString>
#include <optional>
#include <qcorotask.h>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
#include "gittide/ui/asyncrepo.hpp"

namespace gittide::ui {

// Holds the active repository for a window and drives it asynchronously. open()
// is synchronous (cheap); all git work runs through AsyncRepo on the thread pool.
// Coroutine slots take args BY VALUE so they survive a co_await suspension.
class RepoController : public QObject
{
    Q_OBJECT
public:
    explicit RepoController(QObject* parent = nullptr);

    bool isOpen() const
    {
        return m_repo.has_value();
    }
    QString path() const
    {
        return m_path;
    }

public slots:
    void open(const QString& path);
    QCoro::Task<void> refreshStatus();
    QCoro::Task<void> refreshDiff(QString path, gittide::DiffTarget target);
    QCoro::Task<void> stage(gittide::StageSelection sel);
    QCoro::Task<void> unstage(gittide::StageSelection sel);
    QCoro::Task<void> discard(gittide::StageSelection sel);
    QCoro::Task<void> commit(gittide::CommitRequest req);
    QCoro::Task<void> refreshHistory(unsigned limit = 1000);
    QCoro::Task<void> refreshBranches();
    QCoro::Task<void> createBranch(QString name, QString fromOid, bool checkout);
    QCoro::Task<void> switchBranch(QString name);
    QCoro::Task<void> checkoutCommit(QString oid);
    QCoro::Task<void> deleteBranch(QString name, bool force);
    QCoro::Task<void> renameBranch(QString oldName, QString newName);
    // Stage-on-commit (D23): reset index to HEAD, stage each selection, commit,
    // then refresh status + history. Empty selections => no-op + operationFailed.
    QCoro::Task<void> commitSelection(gittide::CommitRequest req,
                                      std::vector<gittide::StageSelection> selections);
    // Read-only history diff:
    QCoro::Task<void> refreshCommitFiles(QString oid);
    QCoro::Task<void> refreshCommitDiff(QString oid, QString path);

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);
    void statusChanged(const std::vector<gittide::FileStatus>& files);
    void diffReady(const QString& path, const gittide::DiffResult& result);
    void committed(const QString& oid);
    void historyReady(gittide::GraphLayout layout);
    void operationFailed(const QString& message);
    void deleteFailedUnmerged(const QString& name);
    void branchesChanged(std::vector<gittide::BranchInfo>);
    void headChanged(gittide::HeadState);
    void commitFilesReady(QString oid, std::vector<gittide::FileStatus> files);
    void commitDiffReady(QString oid, QString path, gittide::DiffResult result);

private:
    std::optional<AsyncRepo> m_repo;
    QString m_path;
};

} // namespace gittide::ui
