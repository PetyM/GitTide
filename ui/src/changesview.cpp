#include "gittide/ui/changesview.hpp"

#include <algorithm>

#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

ChangesView::ChangesView(QWidget* parent)
    : QWidget(parent)
    , m_files(new ChangedFilesList(this))
    , m_message(new QPlainTextEdit(this))
    , m_commitButton(new QPushButton(QStringLiteral("Commit"), this))
{
    qRegisterMetaType<gittide::CommitRequest>();
    qRegisterMetaType<gittide::StageSelection>();
    qRegisterMetaType<std::vector<gittide::StageSelection>>();

    m_files->setMode(ChangedFilesList::Mode::Editable);

    m_message->setObjectName(QStringLiteral("commitMessage"));
    m_commitButton->setObjectName(QStringLiteral("commitButton"));
    m_commitButton->setEnabled(false);
    m_message->setPlaceholderText(QStringLiteral("Commit message…"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_files, 1);
    layout->addWidget(m_message);
    layout->addWidget(m_commitButton);

    connect(m_message, &QPlainTextEdit::textChanged, this, &ChangesView::updateCommitEnabled);

    // A real user click on a row checkbox sets the whole-file state and clears
    // any per-line selection for that file.
    connect(m_files,
            &ChangedFilesList::fileCheckToggled,
            this,
            [this](const QString& path, bool checked)
            {
                auto& fs = m_sel[path];
                fs.state = checked ? ChangedFilesList::Check::Checked : ChangedFilesList::Check::Unchecked;
                fs.checkedLinesByHunk.clear();
                updateCommitEnabled();
            });

    connect(m_files,
            &ChangedFilesList::discardRequested,
            this,
            [this](const QString& path)
            {
                emit discardRequested(gittide::StageSelection{
                    .path = std::filesystem::path(path.toStdString()), .hunkIndex = std::nullopt, .lineIndices = {}});
            });

    connect(m_commitButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                std::vector<gittide::StageSelection> selections;
                for (const auto& [path, fs] : m_sel)
                {
                    // The list row checkbox is the source of truth for whole-file
                    // Checked/Unchecked (setRowCheck is silent, so m_sel may lag);
                    // the model carries the Partial per-hunk line map.
                    const ChangedFilesList::Check rowState = m_files->rowCheck(path);
                    if (rowState == ChangedFilesList::Check::Unchecked)
                        continue;
                    const std::filesystem::path fsPath(path.toStdString());
                    if (rowState == ChangedFilesList::Check::Partial)
                    {
                        for (const auto& [hunk, lines] : fs.checkedLinesByHunk)
                            selections.push_back(
                                gittide::StageSelection{.path = fsPath, .hunkIndex = hunk, .lineIndices = lines});
                    }
                    else // Checked: whole file
                    {
                        selections.push_back(
                            gittide::StageSelection{.path = fsPath, .hunkIndex = std::nullopt, .lineIndices = {}});
                    }
                }
                emit commitRequested(gittide::CommitRequest{.message = commitMessage().toStdString()},
                                     std::move(selections));
            });
}

void ChangesView::setStatus(const std::vector<gittide::FileStatus>& files)
{
    m_files->setFiles(files);
    // Reset the selection model: every file starts fully Checked.
    m_sel.clear();
    for (const auto& f : files)
    {
        const QString path = QString::fromStdString(f.path.generic_string());
        m_sel[path] = FileSel{};
    }
    updateCommitEnabled();
}

QString ChangesView::commitMessage() const
{
    return m_message->toPlainText();
}

void ChangesView::applyLineToggle(const QString& path, int hunkIndex, int lineIndex, bool checked)
{
    auto& fs = m_sel[path];
    fs.state = ChangedFilesList::Check::Partial;

    auto& lines = fs.checkedLinesByHunk[hunkIndex];
    auto it = std::find(lines.begin(), lines.end(), lineIndex);
    if (checked)
    {
        if (it == lines.end())
        {
            lines.push_back(lineIndex);
            std::sort(lines.begin(), lines.end());
        }
    }
    else if (it != lines.end())
    {
        lines.erase(it);
    }

    // Drop now-empty hunks so "no lines anywhere" is detectable.
    for (auto hit = fs.checkedLinesByHunk.begin(); hit != fs.checkedLinesByHunk.end();)
    {
        if (hit->second.empty())
            hit = fs.checkedLinesByHunk.erase(hit);
        else
            ++hit;
    }

    // Collapse: if nothing remains checked, the file is Unchecked. We cannot
    // reliably collapse back to whole-file Checked here because applyLineToggle
    // is fed one line at a time and the model does not carry the total checkable
    // line count per hunk — so a file with all-but-one line checked and a file
    // with every line checked would be indistinguishable. The safe choice is to
    // stay Partial until the user explicitly re-checks the whole file via the row
    // checkbox (which routes through fileCheckToggled and clears the line map).
    if (fs.checkedLinesByHunk.empty())
        fs.state = ChangedFilesList::Check::Unchecked;

    m_files->setRowCheck(path, fs.state);
    updateCommitEnabled();
}

void ChangesView::selectionFor(const QString& path, bool& wholeChecked,
                               std::map<int, std::vector<int>>& checkedLines) const
{
    auto it = m_sel.find(path);
    if (it == m_sel.end())
    {
        wholeChecked = true;
        checkedLines.clear();
        return;
    }
    wholeChecked = it->second.state != ChangedFilesList::Check::Partial;
    checkedLines = it->second.checkedLinesByHunk;
}

void ChangesView::updateCommitEnabled()
{
    const bool hasMessage = !m_message->toPlainText().trimmed().isEmpty();
    const bool anyChecked = std::any_of(m_sel.begin(), m_sel.end(),
                                        [](const auto& kv)
                                        { return kv.second.state != ChangedFilesList::Check::Unchecked; });
    m_commitButton->setEnabled(hasMessage && anyChecked);
}

} // namespace gittide::ui
