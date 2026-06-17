#pragma once
#include <QObject>
#include <QString>
#include <optional>
#include "gitgui/GitRepo.hpp"

namespace gitgui::ui {

// Holds the active repository for a window. Plan 2 scope: open only.
// Stage/diff/commit and async refresh arrive in Plan 3.
class RepoController : public QObject {
    Q_OBJECT
public:
    explicit RepoController(QObject* parent = nullptr);

    bool isOpen() const { return repo_.has_value(); }
    QString path() const { return path_; }

public slots:
    void open(const QString& path);

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);

private:
    std::optional<gitgui::GitRepo> repo_;
    QString path_;
};

}  // namespace gitgui::ui
