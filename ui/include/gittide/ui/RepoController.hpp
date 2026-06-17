#pragma once
#include <QObject>
#include <QString>
#include <optional>
#include <vector>

#include <qcorotask.h>

#include "gittide/ui/AsyncRepo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"

namespace gittide::ui {

// Holds the active repository for a window and drives it asynchronously. open()
// is synchronous (cheap); all git work runs through AsyncRepo on the thread pool.
// Coroutine slots take args BY VALUE so they survive a co_await suspension.
class RepoController : public QObject {
    Q_OBJECT
public:
    explicit RepoController(QObject* parent = nullptr);

    bool isOpen() const { return repo_.has_value(); }
    QString path() const { return path_; }

public slots:
    void open(const QString& path);
    QCoro::Task<void> refreshStatus();
    QCoro::Task<void> refreshDiff(QString path, gittide::DiffTarget target);
    QCoro::Task<void> stage(gittide::StageSelection sel);
    QCoro::Task<void> unstage(gittide::StageSelection sel);
    QCoro::Task<void> discard(gittide::StageSelection sel);
    QCoro::Task<void> commit(gittide::CommitRequest req);
    QCoro::Task<void> refreshHistory(unsigned limit = 1000);

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);
    void statusChanged(const std::vector<gittide::FileStatus>& files);
    void diffReady(const QString& path, const gittide::DiffResult& result);
    void committed(const QString& oid);
    void historyReady(gittide::GraphLayout layout);
    void operationFailed(const QString& message);

private:
    std::optional<AsyncRepo> repo_;
    QString path_;
};

}  // namespace gittide::ui
