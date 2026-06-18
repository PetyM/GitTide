#pragma once
#include <QWidget>
#include <vector>

#include "gittide/branchinfo.hpp"

class QMenu;
class QToolButton;

namespace gittide::ui {

// A thin bar displayed above the tab area showing the current branch. The
// button opens a menu for branch actions (switch, create, rename, delete).
// The widget renders state fed via setBranches / setHead and emits intent
// signals; it does NOT talk to controllers directly.
class BranchBar : public QWidget
{
    Q_OBJECT
public:
    explicit BranchBar(QWidget* parent = nullptr);

    void setBranches(const std::vector<gittide::BranchInfo>& branches);
    void setHead(const gittide::HeadState& head);

signals:
    void switchRequested(const QString& name);
    void createRequested();
    void renameRequested();
    void deleteRequested();

private:
    void rebuildMenu();
    QString labelForHead() const;

    QMenu*                           m_menu    = nullptr;
    QToolButton*                     m_button;
    std::vector<gittide::BranchInfo> m_branches;
    gittide::HeadState               m_head;
};

} // namespace gittide::ui
