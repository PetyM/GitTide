#include "gittide/ui/repoviewmodel.hpp"

#include <algorithm>
#include <utility>

#include <qcorotask.h>

#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repocontroller.hpp"

namespace gittide::ui {

RepoViewModel::RepoViewModel(QObject* parent)
    : QObject(parent)
    , m_controller(new RepoController(this))
    , m_files(new ChangedFilesModel(this))
    , m_diff(new DiffLinesModel(this))
{
    connect(m_controller, &RepoController::statusChanged, this, &RepoViewModel::onStatus);
    connect(m_controller, &RepoController::diffReady, this, &RepoViewModel::onDiff);
    connect(m_controller, &RepoController::headChanged, this, &RepoViewModel::onHead);
    connect(m_controller, &RepoController::branchesChanged, this, &RepoViewModel::onBranches);
    connect(m_controller, &RepoController::operationFailed, this, &RepoViewModel::operationFailed);
    connect(m_diff, &DiffLinesModel::lineToggled, this, &RepoViewModel::onLineToggled);
}

bool RepoViewModel::repoOpen() const
{
    return m_open;
}

QString RepoViewModel::currentBranch() const
{
    return m_branch;
}

QString RepoViewModel::activeFile() const
{
    return m_activeFile;
}

int RepoViewModel::checkedCount() const
{
    return m_files->checkedCount();
}

ChangedFilesModel* RepoViewModel::changedFiles() const
{
    return m_files;
}

DiffLinesModel* RepoViewModel::diffLines() const
{
    return m_diff;
}

void RepoViewModel::open(const QString& path)
{
    m_controller->open(path);
    m_open = true;
    emit changed();
    QCoro::connect(m_controller->refreshStatus(), this, [] {});
    QCoro::connect(m_controller->refreshBranches(), this, [] {});
}

void RepoViewModel::selectFile(const QString& path)
{
    m_activeFile = path;
    emit activeFileChanged();
    QCoro::connect(m_controller->refreshDiff(path, gittide::DiffTarget::WorktreeVsHead), this, [] {});
}

void RepoViewModel::setFileChecked(int row, bool checked)
{
    const QString path = m_files->pathAt(row);
    if (path.isEmpty())
        return;
    FileSel& fs = m_sel[path];
    fs.state    = checked ? ChangedFilesModel::Checked : ChangedFilesModel::Unchecked;
    fs.checkedLinesByHunk.clear();
    m_files->setCheckState(row, fs.state);
    if (path == m_activeFile)
        m_diff->setAllChecked(checked);
    emit checkedChanged();
}

void RepoViewModel::setAllFilesChecked(bool checked)
{
    for (int row = 0; row < m_files->rowCount(QModelIndex()); ++row)
        setFileChecked(row, checked);
}

void RepoViewModel::setLineChecked(int row, bool checked)
{
    // Routes through DiffLinesModel, which emits lineToggled() → onLineToggled().
    m_diff->setLineChecked(row, checked);
}

void RepoViewModel::setAllLinesChecked(bool checked)
{
    if (m_activeFile.isEmpty())
        return;
    m_diff->setAllChecked(checked);
    FileSel& fs = m_sel[m_activeFile];
    fs.checkedLinesByHunk = m_diff->checkedLines();
    recomputeActiveFileState();
}

void RepoViewModel::commit(const QString& summary, const QString& description)
{
    std::vector<gittide::StageSelection> selections;
    for (int row = 0; row < m_files->rowCount(QModelIndex()); ++row)
    {
        const QString path                     = m_files->pathAt(row);
        const ChangedFilesModel::Check rowState = m_files->checkState(row);
        if (rowState == ChangedFilesModel::Unchecked)
            continue;
        const std::filesystem::path fsPath = qstringToPath(path);
        if (rowState == ChangedFilesModel::Partial)
        {
            const auto it = m_sel.find(path);
            if (it != m_sel.end())
                for (const auto& [hunk, lines] : it->second.checkedLinesByHunk)
                    selections.push_back(gittide::StageSelection{.path = fsPath, .hunkIndex = hunk, .lineIndices = lines});
        }
        else // Checked: whole file
        {
            selections.push_back(gittide::StageSelection{.path = fsPath, .hunkIndex = std::nullopt, .lineIndices = {}});
        }
    }

    std::string message = summary.toStdString();
    if (!description.isEmpty())
        message += "\n\n" + description.toStdString();

    QCoro::connect(m_controller->commitSelection(gittide::CommitRequest{.message = message}, std::move(selections)), this,
        [this]() { emit committedOk(); });
}

void RepoViewModel::onStatus(const std::vector<gittide::FileStatus>& files)
{
    m_files->setFiles(files);
    m_sel.clear();
    for (const auto& f : files)
        m_sel[pathToQString(f.path)] = FileSel{ChangedFilesModel::Checked, {}};
    m_activeFile.clear();
    m_activeDiff = {};
    m_diff->clear();
    emit activeFileChanged();
    emit checkedChanged();
}

void RepoViewModel::onDiff(const QString& path, const gittide::DiffResult& result)
{
    if (path != m_activeFile)
        return;
    m_activeDiff      = result;
    const FileSel& fs = m_sel[path];
    m_diff->setDiff(result, fs.checkedLinesByHunk, fs.state == ChangedFilesModel::Checked);
}

void RepoViewModel::onHead(const gittide::HeadState& head)
{
    QString label;
    if (head.unborn)
        label = QStringLiteral("(no commits)");
    else if (head.detached)
        label = QStringLiteral("detached @ ") + QString::fromStdString(head.oid).left(7);
    else if (!head.branch.empty())
        label = QString::fromStdString(head.branch);

    if (label.isEmpty() || label == m_branch)
        return;
    m_branch = label;
    emit branchChanged();
}

void RepoViewModel::onBranches(const std::vector<gittide::BranchInfo>& branches)
{
    for (const auto& b : branches)
    {
        if (b.isHead)
        {
            const QString name = QString::fromStdString(b.name);
            if (!name.isEmpty() && name != m_branch)
            {
                m_branch = name;
                emit branchChanged();
            }
            return;
        }
    }
}

void RepoViewModel::onLineToggled(int hunkIndex, int lineIndex, bool checked)
{
    if (m_activeFile.isEmpty())
        return;
    FileSel& fs = m_sel[m_activeFile];
    auto& lines = fs.checkedLinesByHunk[hunkIndex];
    if (checked)
    {
        if (std::find(lines.begin(), lines.end(), lineIndex) == lines.end())
            lines.push_back(lineIndex);
    }
    else
    {
        lines.erase(std::remove(lines.begin(), lines.end(), lineIndex), lines.end());
        if (lines.empty())
            fs.checkedLinesByHunk.erase(hunkIndex);
    }
    recomputeActiveFileState();
}

void RepoViewModel::recomputeActiveFileState()
{
    if (m_activeFile.isEmpty())
        return;
    const int total   = m_diff->checkableCount();
    const int checked = m_diff->checkedCount();
    ChangedFilesModel::Check state;
    if (checked == 0)
        state = ChangedFilesModel::Unchecked;
    else if (total > 0 && checked == total)
        state = ChangedFilesModel::Checked;
    else
        state = ChangedFilesModel::Partial;

    FileSel& fs = m_sel[m_activeFile];
    fs.state    = state;
    if (state != ChangedFilesModel::Partial)
        fs.checkedLinesByHunk.clear();

    const int row = m_files->rowForPath(m_activeFile);
    if (row >= 0)
        m_files->setCheckState(row, state);
    emit checkedChanged();
}

} // namespace gittide::ui
