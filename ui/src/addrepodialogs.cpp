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
    , m_parentDirEdit(new QLineEdit(this))
    , m_nameEdit(new QLineEdit(this))
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
                    m_parentDirEdit->setText(dir);
            });

    auto* form      = new QFormLayout;
    auto* dirRow    = new QWidget(this);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->addWidget(m_parentDirEdit);
    dirLayout->addWidget(browse);

    form->addRow(QStringLiteral("Parent directory:"), dirRow);
    form->addRow(QStringLiteral("Repository name:"), m_nameEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString InitRepoDialog::parentDir() const
{
    return m_parentDirEdit->text();
}
QString InitRepoDialog::repoName() const
{
    return m_nameEdit->text();
}

CloneRepoDialog::CloneRepoDialog(QWidget* parent)
    : QDialog(parent)
    , m_urlEdit(new QLineEdit(this))
    , m_destEdit(new QLineEdit(this))
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
                    m_destEdit->setText(dir);
            });

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("URL:"), m_urlEdit);

    auto* destRow    = new QWidget(this);
    auto* destLayout = new QHBoxLayout(destRow);
    destLayout->setContentsMargins(0, 0, 0, 0);
    destLayout->addWidget(m_destEdit);
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
    return m_urlEdit->text();
}
QString CloneRepoDialog::dest() const
{
    return m_destEdit->text();
}

} // namespace gittide::ui
