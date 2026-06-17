#include "gittide/ui/ChangesView.hpp"
#include "gittide/ui/DiffView.hpp"
#include "gittide/ui/Metatypes.hpp"

#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

namespace gittide::ui {

namespace {
constexpr int PathRole = Qt::UserRole + 1;

bool any_index_flag(gittide::StatusFlag f) {
    using gittide::has_flag; using gittide::StatusFlag;
    return has_flag(f, StatusFlag::IndexNew) || has_flag(f, StatusFlag::IndexModified) ||
           has_flag(f, StatusFlag::IndexDeleted);
}
bool any_wt_flag(gittide::StatusFlag f) {
    using gittide::has_flag; using gittide::StatusFlag;
    return has_flag(f, StatusFlag::WtNew) || has_flag(f, StatusFlag::WtModified) ||
           has_flag(f, StatusFlag::WtDeleted);
}
}  // namespace

ChangesView::ChangesView(QWidget* parent)
    : QWidget(parent),
      staged_(new QListWidget(this)),
      unstaged_(new QListWidget(this)),
      diff_(new DiffView(this)),
      message_(new QPlainTextEdit(this)),
      commitButton_(new QPushButton(QStringLiteral("Commit"), this)) {
    qRegisterMetaType<gittide::CommitRequest>();
    qRegisterMetaType<gittide::StageSelection>();
    qRegisterMetaType<gittide::DiffTarget>();

    staged_->setObjectName(QStringLiteral("stagedList"));
    unstaged_->setObjectName(QStringLiteral("unstagedList"));
    message_->setObjectName(QStringLiteral("commitMessage"));
    commitButton_->setObjectName(QStringLiteral("commitButton"));
    commitButton_->setEnabled(false);

    message_->setPlaceholderText(QStringLiteral("Commit message…"));

    auto* stagedLabel = new QLabel(QStringLiteral("Staged Changes"), this);
    stagedLabel->setProperty("role", "sectionHeader");
    auto* unstagedLabel = new QLabel(QStringLiteral("Unstaged Changes"), this);
    unstagedLabel->setProperty("role", "sectionHeader");

    auto* leftLayout = new QVBoxLayout;
    leftLayout->addWidget(stagedLabel);
    leftLayout->addWidget(staged_, 1);
    leftLayout->addWidget(unstagedLabel);
    leftLayout->addWidget(unstaged_, 1);
    leftLayout->addWidget(message_);
    leftLayout->addWidget(commitButton_);
    auto* left = new QWidget(this);
    left->setLayout(leftLayout);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(diff_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(staged_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        emit fileSelected(staged_->item(row)->data(PathRole).toString(),
                          gittide::DiffTarget::IndexVsHead);
    });
    connect(unstaged_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        emit fileSelected(unstaged_->item(row)->data(PathRole).toString(),
                          gittide::DiffTarget::WorktreeVsIndex);
    });
    connect(message_, &QPlainTextEdit::textChanged, this, &ChangesView::updateCommitEnabled);
    connect(commitButton_, &QPushButton::clicked, this, [this]() {
        emit commitRequested(gittide::CommitRequest{.message = commitMessage().toStdString()});
    });

    connect(diff_, &DiffView::stageRequested, this, &ChangesView::stageRequested);
    connect(diff_, &DiffView::unstageRequested, this, &ChangesView::unstageRequested);
    connect(diff_, &DiffView::discardRequested, this, &ChangesView::discardRequested);
}

void ChangesView::setStatus(const std::vector<gittide::FileStatus>& files) {
    staged_->clear();
    unstaged_->clear();
    for (const auto& f : files) {
        const QString path = QString::fromStdString(f.path.generic_string());
        if (any_index_flag(f.flags)) {
            auto* item = new QListWidgetItem(path, staged_);
            item->setData(PathRole, path);
        }
        if (any_wt_flag(f.flags)) {
            auto* item = new QListWidgetItem(path, unstaged_);
            item->setData(PathRole, path);
        }
    }
    updateCommitEnabled();
}

void ChangesView::setDiff(const gittide::DiffResult& result, const std::filesystem::path& file) {
    diff_->setDiff(result, file);
}

QString ChangesView::commitMessage() const {
    return message_->toPlainText();
}

void ChangesView::updateCommitEnabled() {
    commitButton_->setEnabled(!message_->toPlainText().trimmed().isEmpty() &&
                              staged_->count() > 0);
}

}  // namespace gittide::ui
