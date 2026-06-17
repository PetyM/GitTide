#include "gittide/ui/DiffView.hpp"
#include "gittide/ui/Metatypes.hpp"

#include <algorithm>

#include <QFont>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace gittide::ui {

namespace {
constexpr int HunkRole = Qt::UserRole + 1;
constexpr int LineRole = Qt::UserRole + 2;
constexpr int OriginRole = Qt::UserRole + 3;
}  // namespace

DiffView::DiffView(QWidget* parent)
    : QWidget(parent), lines_(new QListWidget(this)) {
    qRegisterMetaType<gittide::StageSelection>();
    lines_->setObjectName(QStringLiteral("diffLines"));
    lines_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    lines_->setFont(mono);

    // Action buttons that operate on the current line/hunk selection. Without
    // these the requestStage/Unstage/Discard slots have no GUI trigger.
    auto* stageBtn = new QPushButton(QStringLiteral("Stage"), this);
    auto* unstageBtn = new QPushButton(QStringLiteral("Unstage"), this);
    auto* discardBtn = new QPushButton(QStringLiteral("Discard"), this);
    stageBtn->setObjectName(QStringLiteral("diffStageButton"));
    unstageBtn->setObjectName(QStringLiteral("diffUnstageButton"));
    discardBtn->setObjectName(QStringLiteral("diffDiscardButton"));
    connect(stageBtn, &QPushButton::clicked, this, &DiffView::requestStage);
    connect(unstageBtn, &QPushButton::clicked, this, &DiffView::requestUnstage);
    connect(discardBtn, &QPushButton::clicked, this, &DiffView::requestDiscard);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(stageBtn);
    buttons->addWidget(unstageBtn);
    buttons->addWidget(discardBtn);
    buttons->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(lines_);
    layout->addLayout(buttons);
}

void DiffView::clear() {
    lines_->clear();
    file_.clear();
}

void DiffView::setDiff(const gittide::DiffResult& result, const std::filesystem::path& file) {
    lines_->clear();
    file_ = file;
    for (int h = 0; h < static_cast<int>(result.hunks.size()); ++h) {
        const auto& hunk = result.hunks[h];
        for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i) {
            const auto& ln = hunk.lines[i];
            const QChar prefix = ln.origin == gittide::DiffLineOrigin::Added   ? QChar('+')
                               : ln.origin == gittide::DiffLineOrigin::Removed ? QChar('-')
                                                                              : QChar(' ');
            auto* item = new QListWidgetItem(prefix + QString::fromStdString(ln.text), lines_);
            item->setData(HunkRole, h);
            item->setData(LineRole, i);
            item->setData(OriginRole, static_cast<int>(ln.origin));
        }
    }
}

std::optional<gittide::StageSelection> DiffView::currentSelection() const {
    const auto selected = lines_->selectedItems();
    int hunk = -1;
    std::vector<int> lineIdx;
    for (auto* item : selected) {
        const auto origin = static_cast<gittide::DiffLineOrigin>(item->data(OriginRole).toInt());
        if (origin == gittide::DiffLineOrigin::Context) continue;  // context not stageable
        const int h = item->data(HunkRole).toInt();
        if (hunk == -1) hunk = h;
        if (h != hunk) continue;  // restrict to the first selected hunk
        lineIdx.push_back(item->data(LineRole).toInt());
    }
    if (hunk == -1 || lineIdx.empty()) return std::nullopt;
    std::sort(lineIdx.begin(), lineIdx.end());
    return gittide::StageSelection{.path = file_, .hunkIndex = hunk, .lineIndices = std::move(lineIdx)};
}

void DiffView::requestStage() {
    if (auto sel = currentSelection()) emit stageRequested(*sel);
}
void DiffView::requestUnstage() {
    if (auto sel = currentSelection()) emit unstageRequested(*sel);
}
void DiffView::requestDiscard() {
    if (auto sel = currentSelection()) emit discardRequested(*sel);
}

}  // namespace gittide::ui
