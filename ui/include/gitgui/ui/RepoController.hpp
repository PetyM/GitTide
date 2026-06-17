#pragma once
#include <QObject>
#include <QString>
#include <optional>
#include <vector>

#include <qcorotask.h>

#include "gitgui/ui/AsyncRepo.hpp"
#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"

namespace gitgui::ui {

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
    QCoro::Task<void> refreshDiff(QString path, gitgui::DiffTarget target);
    QCoro::Task<void> stage(gitgui::StageSelection sel);
    QCoro::Task<void> unstage(gitgui::StageSelection sel);
    QCoro::Task<void> discard(gitgui::StageSelection sel);
    QCoro::Task<void> commit(gitgui::CommitRequest req);

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);
    void statusChanged(const std::vector<gitgui::FileStatus>& files);
    void diffReady(const QString& path, const gitgui::DiffResult& result);
    void committed(const QString& oid);
    void operationFailed(const QString& message);

private:
    std::optional<AsyncRepo> repo_;
    QString path_;
};

}  // namespace gitgui::ui
