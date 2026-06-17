// ui/include/gitgui/ui/AddRepoDialogs.hpp
#pragma once
#include <QDialog>

class QLineEdit;

namespace gitgui::ui {

// Dialog for "Initialize new repository" — parent directory + repo name.
class InitRepoDialog : public QDialog {
    Q_OBJECT
public:
    explicit InitRepoDialog(QWidget* parent = nullptr);
    QString parentDir() const;
    QString repoName() const;

private:
    QLineEdit* parentDirEdit_;
    QLineEdit* nameEdit_;
};

}  // namespace gitgui::ui
