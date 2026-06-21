#include "gittide/ui/repoviewmodel.hpp"

#include <utility>

#include <qcorotask.h>

#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repocontroller.hpp"
#include "gittide/ui/historylistmodel.hpp"

namespace gittide::ui {

RepoViewModel::RepoViewModel(QObject* parent)
    : QObject(parent)
    , m_controller(new RepoController(this))
    , m_files(new ChangedFilesModel(this))
    , m_diff(new DiffLinesModel(this))
    , m_branches(new BranchListModel(this))
    , m_history(new HistoryListModel(this))
    , m_commitFiles(new ChangedFilesModel(this))
    , m_commitDiff(new DiffLinesModel(this))
{
    connect(m_controller, &RepoController::statusChanged, this, &RepoViewModel::onStatus);
    connect(m_controller, &RepoController::diffReady, this, &RepoViewModel::onDiff);
    connect(m_controller, &RepoController::headChanged, this, &RepoViewModel::onHead);
    connect(m_controller, &RepoController::branchesChanged, this, &RepoViewModel::onBranches);
    connect(m_controller, &RepoController::historyReady, this, &RepoViewModel::onHistory);
    connect(m_controller, &RepoController::commitFilesReady, this, &RepoViewModel::onCommitFiles);
    connect(m_controller, &RepoController::commitDiffReady, this, &RepoViewModel::onCommitDiff);
    connect(m_controller, &RepoController::operationFailed, this, &RepoViewModel::operationFailed);
    connect(m_controller, &RepoController::deleteFailedUnmerged, this, &RepoViewModel::branchDeleteUnmerged);
    connect(m_diff, &DiffLinesModel::lineToggled, this, &RepoViewModel::onLineToggled);
    connect(m_controller, &RepoController::syncStatusChanged, this,
            [this](gittide::SyncStatus s) { m_sync = s; emit syncStatusChanged(); });
    connect(m_controller, &RepoController::syncBusyChanged, this,
            [this](bool b) { m_syncing = b; emit syncingChanged(); });
    connect(m_controller, &RepoController::pullStrategyChanged, this,
            [this](gittide::PullStrategy s) { m_pullRebase = (s == gittide::PullStrategy::Rebase); emit pullRebaseChanged(); });
    connect(m_controller, &RepoController::authFailed, this,
            [this](QString) { emit authRequired(); });
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

BranchListModel* RepoViewModel::branches() const
{
    return m_branches;
}

HistoryListModel* RepoViewModel::history() const
{
    return m_history;
}

void RepoViewModel::open(const QString& path)
{
    m_headOid.clear();
    m_lastLayout     = {};
    m_headArrived    = false;
    m_historyArrived = false;
    m_controller->open(path);
    m_open = true;
    emit changed();
    QCoro::connect(m_controller->refreshStatus(), this, [] {});
    QCoro::connect(m_controller->refreshBranches(), this, [] {});
    QCoro::connect(m_controller->refreshHistory(), this, [] {});
    QCoro::connect(m_controller->loadPullStrategy(), this, [] {});
    QCoro::connect(m_controller->refreshSyncStatus(), this, [] {});
}

void RepoViewModel::selectFile(const QString& path)
{
    m_activeFile = path;
    emit activeFileChanged();
    QCoro::connect(m_controller->refreshDiff(path, gittide::DiffTarget::WorktreeVsHead), this, [] {});
}

void RepoViewModel::setFileChecked(int row, bool checked)
{
    if (applyFileChecked(row, checked))
        emit checkedChanged();
}

void RepoViewModel::setAllFilesChecked(bool checked)
{
    bool any = false;
    for (int row = 0; row < m_files->rowCount(QModelIndex()); ++row)
        any = applyFileChecked(row, checked) || any;
    if (any)
        emit checkedChanged();
}

bool RepoViewModel::applyFileChecked(int row, bool checked)
{
    const QString path = m_files->pathAt(row);
    if (path.isEmpty())
        return false;
    FileSel& fs = m_sel[path];
    fs.state    = checked ? ChangedFilesModel::Checked : ChangedFilesModel::Unchecked;
    fs.checkedLinesByHunk.clear();
    m_files->setCheckState(row, fs.state);
    if (path == m_activeFile)
        m_diff->setAllChecked(checked);
    return true;
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

void RepoViewModel::switchBranch(const QString& name)
{
    QCoro::connect(m_controller->switchBranch(name), this, [] {});
}

void RepoViewModel::checkoutRemoteBranch(const QString& remoteShorthand)
{
    QCoro::connect(m_controller->checkoutRemoteBranch(remoteShorthand), this, [] {});
}

void RepoViewModel::createBranch(const QString& name, const QString& fromOid, bool checkout)
{
    QCoro::connect(m_controller->createBranch(name, fromOid, checkout), this, [] {});
}

void RepoViewModel::deleteBranch(const QString& name, bool force)
{
    QCoro::connect(m_controller->deleteBranch(name, force), this, [] {});
}

void RepoViewModel::renameBranch(const QString& oldName, const QString& newName)
{
    QCoro::connect(m_controller->renameBranch(oldName, newName), this, [] {});
}

void RepoViewModel::refreshHistory()
{
    QCoro::connect(m_controller->refreshHistory(), this, [] {});
}

void RepoViewModel::onHistory(const gittide::GraphLayout& layout)
{
    m_lastLayout     = layout;
    m_historyArrived = true;
    applyHistoryIfReady();
}

void RepoViewModel::applyHistoryIfReady()
{
    // Apply only once both signals for this open() have landed; whichever arrives
    // last triggers the single setLayout. Works for empty/unborn repos too
    // (empty layout + empty oid → model reset to zero rows).
    if (m_headArrived && m_historyArrived)
        m_history->setLayout(m_lastLayout, m_headOid);
}

void RepoViewModel::onStatus(const std::vector<gittide::FileStatus>& files)
{
    m_files->setFiles(files);
    m_sel.clear();
    for (const auto& f : files)
        m_sel[pathToQString(f.path)] = FileSel{ChangedFilesModel::Checked, {}};
    m_activeFile.clear();
    m_diff->clear();
    emit activeFileChanged();
    emit checkedChanged();
}

void RepoViewModel::onDiff(const QString& path, const gittide::DiffResult& result)
{
    if (path != m_activeFile)
        return;
    const FileSel& fs = m_sel[path];
    m_diff->setDiff(result, fs.checkedLinesByHunk, fs.state == ChangedFilesModel::Checked);
}

void RepoViewModel::onHead(const gittide::HeadState& head)
{
    m_headOid     = QString::fromStdString(head.oid);
    m_headArrived = true;
    applyHistoryIfReady();

    // Always update the real branch ref name (empty when detached or unborn).
    const QString newHeadBranch = head.branch.empty() ? QString() : QString::fromStdString(head.branch);

    QString label;
    if (head.unborn)
        label = QStringLiteral("(no commits)");
    else if (head.detached)
        label = QStringLiteral("detached @ ") + QString::fromStdString(head.oid).left(7);
    else if (!head.branch.empty())
        label = QString::fromStdString(head.branch);

    const bool headBranchChanged = (newHeadBranch != m_headBranch);
    const bool labelChanged      = (!label.isEmpty() && label != m_branch);
    if (!headBranchChanged && !labelChanged)
        return;
    m_headBranch = newHeadBranch;
    if (labelChanged)
        m_branch = label;
    emit branchChanged();
}

void RepoViewModel::onBranches(const std::vector<gittide::BranchInfo>& branches)
{
    m_branches->setBranches(branches);
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

void RepoViewModel::onLineToggled(int /*hunkIndex*/, int /*lineIndex*/, bool /*checked*/)
{
    // The diff model owns which lines are checked; just re-derive file state.
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

    // Derive the per-hunk line selection from the diff model — the single source
    // of truth for which lines are checked — so a Partial commit stages exactly
    // the lines shown checked (only Partial carries an explicit line map).
    FileSel& fs = m_sel[m_activeFile];
    fs.state    = state;
    if (state == ChangedFilesModel::Partial)
        fs.checkedLinesByHunk = m_diff->checkedLines();
    else
        fs.checkedLinesByHunk.clear();

    const int row = m_files->rowForPath(m_activeFile);
    if (row >= 0)
        m_files->setCheckState(row, state);
    emit checkedChanged();
}

ChangedFilesModel* RepoViewModel::commitFiles() const
{
    return m_commitFiles;
}

DiffLinesModel* RepoViewModel::commitDiff() const
{
    return m_commitDiff;
}

QString RepoViewModel::selectedCommit() const
{
    return m_selectedCommit;
}

QString RepoViewModel::activeCommitFile() const
{
    return m_activeCommitFile;
}

void RepoViewModel::selectCommit(const QString& oid)
{
    m_selectedCommit = oid;
    m_activeCommitFile.clear();
    // Clear both panes synchronously so the previous commit's files and diff
    // never linger while the new commit's data loads asynchronously.
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    QCoro::connect(m_controller->refreshCommitFiles(oid), this, [] {});
}

void RepoViewModel::selectCommitFile(const QString& path)
{
    m_activeCommitFile = path;
    emit activeCommitFileChanged();
    QCoro::connect(m_controller->refreshCommitDiff(m_selectedCommit, path), this, [] {});
}

void RepoViewModel::checkoutCommit(const QString& oid)
{
    QCoro::connect(m_controller->checkoutCommit(oid), this, [] {});
}

void RepoViewModel::onCommitFiles(const QString& oid, const std::vector<gittide::FileStatus>& files)
{
    if (oid != m_selectedCommit)
        return;
    m_commitFiles->setFiles(files);
}

void RepoViewModel::onCommitDiff(const QString& oid, const QString& path, const gittide::DiffResult& result)
{
    if (oid != m_selectedCommit || path != m_activeCommitFile)
        return;
    // Read-only: no checked lines, not whole-file-checked. The QML detail view
    // hides the per-line checkbox column.
    m_commitDiff->setDiff(result, {}, false);
}

void RepoViewModel::fetch()
{
    m_pendingOp = PendingOp::Fetch;
    QCoro::connect(m_controller->fetch(m_sessionCred), this, [] {});
}

void RepoViewModel::pull()
{
    m_pendingOp = PendingOp::Pull;
    QCoro::connect(m_controller->pull(m_sessionCred), this, [] {});
}

void RepoViewModel::push()
{
    if (m_headBranch.isEmpty())
    {
        emit operationFailed(QStringLiteral("Cannot push: HEAD is detached or unborn — switch to a branch first."));
        return;
    }
    m_pendingOp = PendingOp::Push;
    QCoro::connect(m_controller->push(m_headBranch, /*setUpstream=*/false, m_sessionCred), this, [] {});
}

void RepoViewModel::publishBranch()
{
    if (m_headBranch.isEmpty())
    {
        emit operationFailed(QStringLiteral("Cannot push: HEAD is detached or unborn — switch to a branch first."));
        return;
    }
    m_pendingOp = PendingOp::Publish;
    QCoro::connect(m_controller->push(m_headBranch, /*setUpstream=*/true, m_sessionCred), this, [] {});
}

void RepoViewModel::submitCredentials(const QString& username, const QString& token)
{
    m_sessionCred.username    = username.toStdString();
    m_sessionCred.password    = token.toStdString();
    m_sessionCred.sshUseAgent = true;
    switch (m_pendingOp)
    {
    case PendingOp::Fetch:   fetch(); break;
    case PendingOp::Pull:    pull(); break;
    case PendingOp::Push:    push(); break;
    case PendingOp::Publish: publishBranch(); break;
    case PendingOp::None:    break;
    }
}

void RepoViewModel::setPullRebase(bool rebase)
{
    QCoro::connect(m_controller->setPullStrategy(rebase ? gittide::PullStrategy::Rebase
                                                        : gittide::PullStrategy::FastForwardOnly),
                   this, [] {});
}

} // namespace gittide::ui
