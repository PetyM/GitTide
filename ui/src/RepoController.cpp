#include "gitgui/ui/RepoController.hpp"
#include <filesystem>

namespace gitgui::ui {

RepoController::RepoController(QObject* parent) : QObject(parent) {}

void RepoController::open(const QString& path) {
    const std::filesystem::path p(path.toStdString());
    auto result = gitgui::GitRepo::open(p);
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

}  // namespace gitgui::ui
