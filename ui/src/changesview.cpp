#include "gittide/ui/changesview.hpp"

#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "gittide/ui/diffview.hpp"
#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

namespace {
constexpr int PathRole = Qt::UserRole + 1;

bool anyIndexFlag(gittide::StatusFlag f)
{
    using gittide::hasFlag;
    using gittide::StatusFlag;
    return hasFlag(f, StatusFlag::IndexNew) || hasFlag(f, StatusFlag::IndexModified) || hasFlag(f, StatusFlag::IndexDeleted);
}
bool anyWtFlag(gittide::StatusFlag f)
{
    using gittide::hasFlag;
    using gittide::StatusFlag;
    return hasFlag(f, StatusFlag::WtNew) || hasFlag(f, StatusFlag::WtModified) || hasFlag(f, StatusFlag::WtDeleted);
}
} // namespace

ChangesView::ChangesView(QWidget* parent)
    : QWidget(parent)
    , m_staged(new QListWidget(this))
    , m_unstaged(new QListWidget(this))
    , m_diff(new DiffView(this))
    , m_message(new QPlainTextEdit(this))
    , m_commitButton(new QPushButton(QStringLiteral("Commit"), this))
{
    qRegisterMetaType<gittide::CommitRequest>();
    qRegisterMetaType<gittide::StageSelection>();
    qRegisterMetaType<gittide::DiffTarget>();

    m_diff->setMode(DiffView::Mode::Editable);

    m_staged->setObjectName(QStringLiteral("stagedList"));
    m_unstaged->setObjectName(QStringLiteral("unstagedList"));
    m_message->setObjectName(QStringLiteral("commitMessage"));
    m_commitButton->setObjectName(QStringLiteral("commitButton"));
    m_commitButton->setEnabled(false);

    m_message->setPlaceholderText(QStringLiteral("Commit message…"));

    auto* stagedLabel = new QLabel(QStringLiteral("Staged Changes"), this);
    stagedLabel->setProperty("role", "sectionHeader");
    auto* unstagedLabel = new QLabel(QStringLiteral("Unstaged Changes"), this);
    unstagedLabel->setProperty("role", "sectionHeader");

    auto* leftLayout = new QVBoxLayout;
    leftLayout->addWidget(stagedLabel);
    leftLayout->addWidget(m_staged, 1);
    leftLayout->addWidget(unstagedLabel);
    leftLayout->addWidget(m_unstaged, 1);
    leftLayout->addWidget(m_message);
    leftLayout->addWidget(m_commitButton);
    auto* left = new QWidget(this);
    left->setLayout(leftLayout);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(m_diff);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(m_staged,
            &QListWidget::currentRowChanged,
            this,
            [this](int row)
            {
                if (row < 0)
                    return;
                emit fileSelected(m_staged->item(row)->data(PathRole).toString(), gittide::DiffTarget::IndexVsHead);
            });
    connect(m_unstaged,
            &QListWidget::currentRowChanged,
            this,
            [this](int row)
            {
                if (row < 0)
                    return;
                emit fileSelected(m_unstaged->item(row)->data(PathRole).toString(), gittide::DiffTarget::WorktreeVsIndex);
            });
    connect(m_message, &QPlainTextEdit::textChanged, this, &ChangesView::updateCommitEnabled);
    connect(m_commitButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                emit commitRequested(gittide::CommitRequest{.message = commitMessage().toStdString()});
            });

    // DiffView no longer emits stageRequested/unstageRequested (removed in Task 5;
    // the commit-from-selection model wires these differently in Tasks 6/7).
    connect(m_diff, &DiffView::discardRequested, this, &ChangesView::discardRequested);
}

void ChangesView::setStatus(const std::vector<gittide::FileStatus>& files)
{
    m_staged->clear();
    m_unstaged->clear();
    for (const auto& f : files)
    {
        const QString path = QString::fromStdString(f.path.generic_string());
        if (anyIndexFlag(f.flags))
        {
            auto* item = new QListWidgetItem(path, m_staged);
            item->setData(PathRole, path);
        }
        if (anyWtFlag(f.flags))
        {
            auto* item = new QListWidgetItem(path, m_unstaged);
            item->setData(PathRole, path);
        }
    }
    updateCommitEnabled();
}

void ChangesView::setDiff(const gittide::DiffResult& result, const std::filesystem::path& file)
{
    // Task 5: DiffView::setDiff now requires mode + checkedLines. Pass Editable
    // mode with wholeChecked=true and empty checkedLines (all lines checked by
    // default). Tasks 6/7 will wire per-line selection state properly.
    m_diff->setDiff(result, file, /*wholeChecked=*/true, {});
}

QString ChangesView::commitMessage() const
{
    return m_message->toPlainText();
}

void ChangesView::updateCommitEnabled()
{
    m_commitButton->setEnabled(!m_message->toPlainText().trimmed().isEmpty() && m_staged->count() > 0);
}

} // namespace gittide::ui
