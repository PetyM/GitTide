// ui/include/gittide/ui/branchdialogs.hpp
#pragma once
#include <QDialog>
#include <QString>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace gittide::ui {

// Returns true if s is a syntactically acceptable git branch name segment.
// Rules (pragmatic subset, UI-only — does NOT call libgit2):
//   - non-empty
//   - no whitespace characters
//   - none of the characters: ~ ^ : ? * [ \
//   - does not start with '-'
bool isValidBranchNameInput(const QString& s);

// ---- New branch dialog ----------------------------------------------------

struct NewBranchChoice
{
    QString name;
    bool    checkout = true;
    bool    accepted = false;
};

class NewBranchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewBranchDialog(const QString& fromLabel, QWidget* parent = nullptr);

    QString name() const;
    bool    checkout() const;

private:
    void onTextChanged(const QString& text);

    QLineEdit*        m_nameEdit;
    QCheckBox*        m_checkoutBox;
    QLabel*           m_validationLabel;
    QDialogButtonBox* m_buttons;
};

NewBranchChoice askNewBranch(QWidget* parent, const QString& fromLabel);

// ---- Rename branch dialog -------------------------------------------------

struct RenameChoice
{
    QString name;
    bool    accepted = false;
};

class RenameBranchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RenameBranchDialog(const QString& current, QWidget* parent = nullptr);

    QString name() const;

private:
    void onTextChanged(const QString& text);

    QLineEdit*        m_nameEdit;
    QLabel*           m_validationLabel;
    QDialogButtonBox* m_buttons;
};

RenameChoice askRenameBranch(QWidget* parent, const QString& current);

// ---- Delete branch dialog -------------------------------------------------

struct DeleteChoice
{
    bool accepted = false;
    bool force    = false;
};

DeleteChoice askDeleteBranch(QWidget* parent, const QString& name, bool unmerged);

} // namespace gittide::ui
