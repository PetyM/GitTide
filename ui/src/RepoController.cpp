#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/Metatypes.hpp"

#include <filesystem>

namespace gitgui::ui {

RepoController::RepoController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<std::vector<gitgui::FileStatus>>();
    qRegisterMetaType<gitgui::DiffResult>();
    qRegisterMetaType<gitgui::StageSelection>();
    qRegisterMetaType<gitgui::CommitRequest>();
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

QCoro::Task<void> RepoController::refreshDiff(QString path, gitgui::DiffTarget target) {
    if (!repo_) co_return;
    auto result = co_await repo_->diff(target, std::filesystem::path(path.toStdString()));
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit diffReady(path, *result);
}

QCoro::Task<void> RepoController::stage(gitgui::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->stage(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::unstage(gitgui::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->unstage(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::discard(gitgui::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->discard(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::commit(gitgui::CommitRequest req) {
    if (!repo_) co_return;
    auto result = co_await repo_->commit(req);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit committed(QString::fromStdString(*result));
    co_await refreshStatus();
}

}  // namespace gitgui::ui
