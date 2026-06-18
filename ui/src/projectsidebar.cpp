#include "gittide/ui/projectsidebar.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/projectlistmodel.hpp"
#include "gittide/ui/repolistmodel.hpp"

namespace gittide::ui {

static constexpr auto kSentinel = "__new__";

ProjectSidebar::ProjectSidebar(ProjectController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_switcher(new QComboBox(this))
    , m_openInNewWindowBtn(new QToolButton(this))
    , m_removeProjectBtn(new QToolButton(this))
    , m_repoList(new QTreeView(this))
    , m_addExistingBtn(new QToolButton(this))
    , m_initRepoBtn(new QToolButton(this))
    , m_cloneBtn(new QToolButton(this))
{

    m_switcher->setObjectName(QStringLiteral("projectSwitcher"));
    m_switcher->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_openInNewWindowBtn->setObjectName(QStringLiteral("openInNewWindowButton"));
    m_openInNewWindowBtn->setText(QStringLiteral("⧉"));
    m_openInNewWindowBtn->setToolTip(QStringLiteral("Open current project in a new window"));

    m_removeProjectBtn->setObjectName(QStringLiteral("removeProjectButton"));
    m_removeProjectBtn->setText(QStringLiteral("✕"));
    m_removeProjectBtn->setToolTip(QStringLiteral("Remove current project"));

    m_repoList->setObjectName(QStringLiteral("repoList"));
    m_repoList->setModel(m_controller->repos());
    m_repoList->header()->hide();
    m_repoList->setRootIsDecorated(true);
    m_repoList->setContextMenuPolicy(Qt::CustomContextMenu);

    m_addExistingBtn->setObjectName(QStringLiteral("addExistingButton"));
    m_addExistingBtn->setText(QStringLiteral("Add"));
    m_addExistingBtn->setToolTip(QStringLiteral("Add existing repository"));

    m_initRepoBtn->setObjectName(QStringLiteral("initRepoButton"));
    m_initRepoBtn->setText(QStringLiteral("Init"));
    m_initRepoBtn->setToolTip(QStringLiteral("Initialize new repository"));

    m_cloneBtn->setObjectName(QStringLiteral("cloneButton"));
    m_cloneBtn->setText(QStringLiteral("Clone"));
    m_cloneBtn->setToolTip(QStringLiteral("Clone repository from URL"));

    auto* switcherRow    = new QWidget(this);
    auto* switcherLayout = new QHBoxLayout(switcherRow);
    switcherLayout->setContentsMargins(0, 0, 0, 0);
    switcherLayout->addWidget(m_switcher);
    switcherLayout->addWidget(m_openInNewWindowBtn);
    switcherLayout->addWidget(m_removeProjectBtn);

    auto* toolbar       = new QWidget(this);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->addWidget(m_addExistingBtn);
    toolbarLayout->addWidget(m_initRepoBtn);
    toolbarLayout->addWidget(m_cloneBtn);
    toolbarLayout->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(switcherRow);
    layout->addWidget(m_repoList, /*stretch=*/1);
    layout->addWidget(toolbar);

    // Populate combo and keep it in sync
    syncCombo();
    connect(m_controller,
            &ProjectController::projectCreated,
            this,
            [this](const QString&)
            {
                syncCombo();
            });
    connect(m_controller,
            &ProjectController::projectActivated,
            this,
            [this](const QString&)
            {
                syncCombo();
            });
    connect(m_controller,
            &ProjectController::projectRemoved,
            this,
            [this](const QString&)
            {
                syncCombo();
            });

    // Selecting a repo row emits repoSelected.
    connect(m_repoList->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current)
            {
                if (!current.isValid())
                    return;
                emit repoSelected(current.data(RepoListModel::PathRole).toString());
            });

    // Combo selection — detect sentinel.
    connect(m_switcher,
            &QComboBox::currentIndexChanged,
            this,
            [this](int row)
            {
                if (row < 0)
                    return;
                const QString id = m_switcher->itemData(row).toString();
                if (id == QString::fromLatin1(kSentinel))
                {
                    emit createProjectRequested();
                    syncCombo(); // restores selection to current active project
                    return;
                }
                m_controller->activate(id);
            });

    // Remove project button
    connect(m_removeProjectBtn,
            &QToolButton::clicked,
            this,
            [this]()
            {
                if (m_controller->activeProjectId().isEmpty())
                    return;
                const auto answer =
                    QMessageBox::question(this,
                                          QStringLiteral("Remove Project"),
                                          QStringLiteral("Remove the current project? Repositories on disk are not affected."),
                                          QMessageBox::Yes | QMessageBox::Cancel,
                                          QMessageBox::Cancel);
                if (answer == QMessageBox::Yes)
                    m_controller->removeProject();
            });

    // Repo list context menu
    connect(m_repoList,
            &QTreeView::customContextMenuRequested,
            this,
            [this](const QPoint& pos)
            {
                const QModelIndex idx = m_repoList->indexAt(pos);
                if (!idx.isValid())
                    return;
                const QString path = idx.data(RepoListModel::PathRole).toString();
                if (path.isEmpty())
                    return;
                QMenu menu(this);
                auto* removeAction = menu.addAction(QStringLiteral("Remove from project"));
                if (menu.exec(m_repoList->viewport()->mapToGlobal(pos)) == removeAction)
                {
                    m_controller->removeRepo(path);
                }
            });

    // Switcher-row buttons
    connect(m_openInNewWindowBtn, &QToolButton::clicked, this, &ProjectSidebar::requestOpenInNewWindow);

    // Toolbar buttons
    connect(m_addExistingBtn, &QToolButton::clicked, this, &ProjectSidebar::addExistingRequested);
    connect(m_initRepoBtn, &QToolButton::clicked, this, &ProjectSidebar::initRepoRequested);
    connect(m_cloneBtn, &QToolButton::clicked, this, &ProjectSidebar::cloneRepoRequested);
}

void ProjectSidebar::syncCombo()
{
    const QSignalBlocker blocker(m_switcher);
    m_switcher->clear();
    auto* model = m_controller->projects();
    for (int i = 0; i < model->rowCount(); ++i)
    {
        m_switcher->addItem(model->data(model->index(i), Qt::DisplayRole).toString(),
                            model->data(model->index(i), ProjectListModel::IdRole));
    }
    m_switcher->addItem(QStringLiteral("New project…"), QString::fromLatin1(kSentinel));

    // Restore active project selection
    const QString activeId = m_controller->activeProjectId();
    if (!activeId.isEmpty())
    {
        for (int i = 0; i < model->rowCount(); ++i)
        {
            if (m_switcher->itemData(i).toString() == activeId)
            {
                m_switcher->setCurrentIndex(i);
                return;
            }
        }
    }
    m_switcher->setCurrentIndex(-1);
}

void ProjectSidebar::requestOpenInNewWindow()
{
    const QString id = m_controller->activeProjectId();
    if (!id.isEmpty())
        emit openInNewWindowRequested(id);
}

} // namespace gittide::ui
