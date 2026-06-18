// ui/src/branchdialogs.cpp
#include "gittide/ui/branchdialogs.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace gittide::ui {

// ---------------------------------------------------------------------------
// Validation predicate — pure, no exec(), unit-testable
// ---------------------------------------------------------------------------

bool isValidBranchNameInput(const QString& s)
{
    if (s.isEmpty())
        return false;

    // Must not start with '-'
    if (s.startsWith(QLatin1Char('-')))
        return false;

    // Forbidden characters: ~ ^ : ? * [ \  and any whitespace
    static const QString kForbidden = QStringLiteral("~^:?*[\\");
    for (int i = 0; i < s.size(); ++i)
    {
        const QChar ch = s.at(i);
        if (ch.isSpace() || kForbidden.contains(ch))
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// NewBranchDialog
// ---------------------------------------------------------------------------

NewBranchDialog::NewBranchDialog(const QString& fromLabel, QWidget* parent)
    : QDialog(parent)
    , m_nameEdit(new QLineEdit(this))
    , m_checkoutBox(new QCheckBox(QStringLiteral("Switch to it after creating"), this))
    , m_validationLabel(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(QStringLiteral("New Branch"));
    setObjectName(QStringLiteral("newBranchDialog"));

    m_checkoutBox->setChecked(true);

    m_validationLabel->setObjectName(QStringLiteral("branchValidationLabel"));
    m_validationLabel->setProperty("role", QStringLiteral("subtext"));
    m_validationLabel->setVisible(false);

    auto* form = new QFormLayout;
    if (!fromLabel.isEmpty())
        form->addRow(QStringLiteral("From:"), new QLabel(fromLabel, this));
    form->addRow(QStringLiteral("Branch name:"), m_nameEdit);
    form->addRow(QString(), m_validationLabel);
    form->addRow(QString(), m_checkoutBox);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_buttons);

    // OK starts disabled (name empty)
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(m_nameEdit, &QLineEdit::textChanged, this, &NewBranchDialog::onTextChanged);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString NewBranchDialog::name() const
{
    return m_nameEdit->text().trimmed();
}

bool NewBranchDialog::checkout() const
{
    return m_checkoutBox->isChecked();
}

void NewBranchDialog::onTextChanged(const QString& text)
{
    const bool valid = isValidBranchNameInput(text);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);

    if (text.isEmpty())
    {
        m_validationLabel->setVisible(false);
    }
    else if (!valid)
    {
        m_validationLabel->setText(QStringLiteral("Name must not contain spaces, ~^:?*[\\ or start with -"));
        m_validationLabel->setVisible(true);
    }
    else
    {
        m_validationLabel->setVisible(false);
    }
}

NewBranchChoice askNewBranch(QWidget* parent, const QString& fromLabel)
{
    NewBranchDialog dlg(fromLabel, parent);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    return NewBranchChoice{dlg.name(), dlg.checkout(), true};
}

// ---------------------------------------------------------------------------
// RenameBranchDialog
// ---------------------------------------------------------------------------

RenameBranchDialog::RenameBranchDialog(const QString& current, QWidget* parent)
    : QDialog(parent)
    , m_nameEdit(new QLineEdit(this))
    , m_validationLabel(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(QStringLiteral("Rename Branch"));
    setObjectName(QStringLiteral("renameBranchDialog"));

    m_nameEdit->setText(current);
    m_nameEdit->selectAll();

    m_validationLabel->setObjectName(QStringLiteral("renameValidationLabel"));
    m_validationLabel->setProperty("role", QStringLiteral("subtext"));
    m_validationLabel->setVisible(false);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("New name:"), m_nameEdit);
    form->addRow(QString(), m_validationLabel);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_buttons);

    // Start enabled only if the prefilled name is already valid
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(isValidBranchNameInput(current));

    connect(m_nameEdit, &QLineEdit::textChanged, this, &RenameBranchDialog::onTextChanged);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString RenameBranchDialog::name() const
{
    return m_nameEdit->text().trimmed();
}

void RenameBranchDialog::onTextChanged(const QString& text)
{
    const bool valid = isValidBranchNameInput(text);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);

    if (text.isEmpty())
    {
        m_validationLabel->setVisible(false);
    }
    else if (!valid)
    {
        m_validationLabel->setText(QStringLiteral("Name must not contain spaces, ~^:?*[\\ or start with -"));
        m_validationLabel->setVisible(true);
    }
    else
    {
        m_validationLabel->setVisible(false);
    }
}

RenameChoice askRenameBranch(QWidget* parent, const QString& current)
{
    RenameBranchDialog dlg(current, parent);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    return RenameChoice{dlg.name(), true};
}

// ---------------------------------------------------------------------------
// askDeleteBranch — no dialog class needed; built inline
// ---------------------------------------------------------------------------

DeleteChoice askDeleteBranch(QWidget* parent, const QString& name, bool unmerged)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Delete Branch"));
    dlg.setObjectName(QStringLiteral("deleteBranchDialog"));

    auto* infoLabel = new QLabel(
        QStringLiteral("Delete branch <b>%1</b>?").arg(name.toHtmlEscaped()), &dlg);
    infoLabel->setTextFormat(Qt::RichText);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(infoLabel);

    // Standard delete (only shown when branch IS merged, or as the primary action)
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);

    DeleteChoice result;

    if (!unmerged)
    {
        // Simple case: branch is merged — one Delete button
        auto* deleteBtn = buttonBox->addButton(QStringLiteral("Delete"),
                                               QDialogButtonBox::DestructiveRole);
        deleteBtn->setObjectName(QStringLiteral("deleteButton"));
        QObject::connect(deleteBtn,
                         &QPushButton::clicked,
                         &dlg,
                         [&dlg, &result]()
                         {
                             result.accepted = true;
                             result.force    = false;
                             dlg.accept();
                         });
    }
    else
    {
        // Two-step: branch is unmerged — show warning + force-delete button
        auto* warningLabel =
            new QLabel(QStringLiteral("This branch has not been fully merged."), &dlg);
        warningLabel->setObjectName(QStringLiteral("unmergedWarningLabel"));
        warningLabel->setProperty("role", QStringLiteral("subtext"));
        layout->addWidget(warningLabel);

        auto* forceDeleteBtn =
            buttonBox->addButton(QStringLiteral("Delete Anyway (not fully merged)"),
                                 QDialogButtonBox::DestructiveRole);
        forceDeleteBtn->setObjectName(QStringLiteral("forceDeleteButton"));
        QObject::connect(forceDeleteBtn,
                         &QPushButton::clicked,
                         &dlg,
                         [&dlg, &result]()
                         {
                             result.accepted = true;
                             result.force    = true;
                             dlg.accept();
                         });
    }

    layout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
    return result;
}

} // namespace gittide::ui
