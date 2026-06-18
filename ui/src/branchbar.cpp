#include "gittide/ui/branchbar.hpp"

#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

namespace gittide::ui {

BranchBar::BranchBar(QWidget* parent)
    : QWidget(parent)
    , m_menu(new QMenu(this))
    , m_button(new QToolButton(this))
{
    setObjectName(QStringLiteral("branchBar"));
    m_button->setObjectName(QStringLiteral("currentBranchButton"));
    m_button->setPopupMode(QToolButton::InstantPopup);
    m_button->setText(QStringLiteral("(no commits)"));
    m_button->setMenu(m_menu);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_button);
    layout->addStretch();
}

QString BranchBar::labelForHead() const
{
    if (m_head.unborn)
        return QStringLiteral("(no commits)");
    if (m_head.detached)
    {
        const QString oid = QString::fromStdString(m_head.oid).left(7);
        return QStringLiteral("detached @ ") + oid;
    }
    return QString::fromStdString(m_head.branch);
}

void BranchBar::setHead(const gittide::HeadState& head)
{
    m_head = head;
    m_button->setText(labelForHead());
}

void BranchBar::setBranches(const std::vector<gittide::BranchInfo>& branches)
{
    m_branches = branches;
    rebuildMenu();
}

void BranchBar::rebuildMenu()
{
    m_menu->clear();

    for (const auto& branch : m_branches)
    {
        const QString name = QString::fromStdString(branch.name);
        QAction* action    = m_menu->addAction(name);
        action->setCheckable(true);
        action->setChecked(branch.isHead);
        connect(action,
                &QAction::triggered,
                this,
                [this, name]()
                {
                    emit switchRequested(name);
                });
    }

    m_menu->addSeparator();

    QAction* newAction = m_menu->addAction(QStringLiteral("New branch…"));
    connect(newAction, &QAction::triggered, this, &BranchBar::createRequested);

    QAction* renameAction = m_menu->addAction(QStringLiteral("Rename current…"));
    connect(renameAction, &QAction::triggered, this, &BranchBar::renameRequested);

    QAction* deleteAction = m_menu->addAction(QStringLiteral("Delete…"));
    connect(deleteAction, &QAction::triggered, this, &BranchBar::deleteRequested);
}

} // namespace gittide::ui
