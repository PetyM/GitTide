// ui/include/gittide/ui/addrepodialogs.hpp
#pragma once
#include <QDialog>

class QLineEdit;

namespace gittide::ui {

// Dialog for "Initialize new repository" — parent directory + repo name.
class InitRepoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit InitRepoDialog(QWidget* parent = nullptr);
    QString parentDir() const;
    QString repoName() const;

private:
    QLineEdit* m_parentDirEdit;
    QLineEdit* m_nameEdit;
};

// Dialog for "Clone repository" — remote URL + local destination.
class CloneRepoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CloneRepoDialog(QWidget* parent = nullptr);
    QString url() const;
    QString dest() const;

private:
    QLineEdit* m_urlEdit;
    QLineEdit* m_destEdit;
};

} // namespace gittide::ui
