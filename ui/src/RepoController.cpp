#include "gittide/ui/RepoController.hpp"
#include "gittide/ui/Metatypes.hpp"

#include <filesystem>
#include <utility>

#include "gittide/graphbuilder.hpp"

namespace gittide::ui {

RepoController::RepoController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<std::vector<gittide::FileStatus>>();
    qRegisterMetaType<gittide::DiffResult>();
    qRegisterMetaType<gittide::StageSelection>();
    qRegisterMetaType<gittide::CommitRequest>();
    qRegisterMetaType<gittide::GraphLayout>();
}

void RepoController::open(const QString& path) {
    auto result = AsyncRepo::open(std::filesystem::path(path.toStdString()));
    if (!result) {
        repo_.reset();
        path_.clear();
        emit repoFailed(path, QString::fromStdString(result.error().message));
        return;
    }
    repo_.emplace(std::move(*result));
    path_ = path;
    emit repoOpened(path);
}

QCoro::Task<void> RepoController::refreshStatus() {
    if (!repo_) co_return;
    auto result = co_await repo_->status();
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit statusChanged(*result);
}

QCoro::Task<void> RepoController::refreshDiff(QString path, gittide::DiffTarget target) {
    if (!repo_) co_return;
    auto result = co_await repo_->diff(target, std::filesystem::path(path.toStdString()));
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit diffReady(path, *result);
}

QCoro::Task<void> RepoController::stage(gittide::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->stage(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::unstage(gittide::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->unstage(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::discard(gittide::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->discard(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::commit(gittide::CommitRequest req) {
    if (!repo_) co_return;
    auto result = co_await repo_->commit(req);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit committed(QString::fromStdString(*result));
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::refreshHistory(unsigned limit) {
    if (!repo_) co_return;
    auto result = co_await repo_->log(limit);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit historyReady(gittide::GraphBuilder::build(std::move(*result)));
}

}  // namespace gittide::ui
