// ui/src/AddRepoDialogs.cpp
#include "gittide/ui/addrepodialogs.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace gittide::ui {

InitRepoDialog::InitRepoDialog(QWidget* parent)
    : QDialog(parent)
    , parentDirEdit_(new QLineEdit(this))
    , nameEdit_(new QLineEdit(this))
{
    setWindowTitle(QStringLiteral("Initialize Repository"));

    auto* browse = new QPushButton(QStringLiteral("Browse…"), this);
    connect(browse,
            &QPushButton::clicked,
            this,
            [this]
            {
                const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select parent directory"));
                if (!dir.isEmpty())
                    parentDirEdit_->setText(dir);
            });

    auto* form      = new QFormLayout;
    auto* dirRow    = new QWidget(this);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->addWidget(parentDirEdit_);
    dirLayout->addWidget(browse);

    form->addRow(QStringLiteral("Parent directory:"), dirRow);
    form->addRow(QStringLiteral("Repository name:"), nameEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString InitRepoDialog::parentDir() const
{
    return parentDirEdit_->text();
}
QString InitRepoDialog::repoName() const
{
    return nameEdit_->text();
}

CloneRepoDialog::CloneRepoDialog(QWidget* parent)
    : QDialog(parent)
    , urlEdit_(new QLineEdit(this))
    , destEdit_(new QLineEdit(this))
{
    setWindowTitle(QStringLiteral("Clone Repository"));

    auto* browse = new QPushButton(QStringLiteral("Browse…"), this);
    connect(browse,
            &QPushButton::clicked,
            this,
            [this]
            {
                const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select destination"));
                if (!dir.isEmpty())
                    destEdit_->setText(dir);
            });

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("URL:"), urlEdit_);

    auto* destRow    = new QWidget(this);
    auto* destLayout = new QHBoxLayout(destRow);
    destLayout->setContentsMargins(0, 0, 0, 0);
    destLayout->addWidget(destEdit_);
    destLayout->addWidget(browse);
    form->addRow(QStringLiteral("Destination:"), destRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString CloneRepoDialog::url() const
{
    return urlEdit_->text();
}
QString CloneRepoDialog::dest() const
{
    return destEdit_->text();
}

} // namespace gittide::ui
