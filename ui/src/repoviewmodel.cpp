#include "gittide/ui/repoviewmodel.hpp"

#include <algorithm>
#include <utility>

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QUrl>

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
    , m_graph(new HistoryListModel(this))
    , m_commitFiles(new ChangedFilesModel(this))
    , m_commitDiff(new DiffLinesModel(this))
{
    connect(m_controller, &RepoController::statusChanged, this, &RepoViewModel::onStatus);
    connect(m_controller, &RepoController::diffReady, this, &RepoViewModel::onDiff);
    connect(m_controller, &RepoController::headChanged, this, &RepoViewModel::onHead);
    connect(m_controller, &RepoController::branchesChanged, this, &RepoViewModel::onBranches);
    connect(m_controller, &RepoController::historyReady, this, &RepoViewModel::onHistory);
    connect(m_controller, &RepoController::graphReady,   this, &RepoViewModel::onGraph);
    connect(m_controller, &RepoController::refTipsReady, this, &RepoViewModel::onRefTips);
    connect(m_controller, &RepoController::commitFilesReady, this, &RepoViewModel::onCommitFiles);
    connect(m_controller, &RepoController::commitDiffReady, this, &RepoViewModel::onCommitDiff);
    connect(m_controller, &RepoController::rangeFilesReady, this, &RepoViewModel::onRangeFiles);
    connect(m_controller, &RepoController::rangeDiffReady, this, &RepoViewModel::onRangeDiff);
    connect(m_controller, &RepoController::operationFailed, this, &RepoViewModel::operationFailed);
    connect(m_controller, &RepoController::deleteFailedUnmerged, this, &RepoViewModel::branchDeleteUnmerged);
    connect(m_diff, &DiffLinesModel::lineToggled, this, &RepoViewModel::onLineToggled);
    connect(m_controller, &RepoController::syncStatusChanged, this,
            [this](gittide::SyncStatus s) { m_sync = s; emit syncStatusChanged(); });
    connect(m_controller, &RepoController::syncBusyChanged, this,
            [this](bool b)
            {
                m_syncing = b;
                // Clear stale counts at each transfer's start and end so the bar
                // starts indeterminate and doesn't linger after completion.
                m_syncReceived = 0;
                m_syncTotal    = 0;
                emit syncProgressChanged();
                emit syncingChanged();
            });
    connect(m_controller, &RepoController::syncProgressChanged, this,
            [this](unsigned received, unsigned total)
            {
                m_syncReceived = static_cast<int>(received);
                m_syncTotal    = static_cast<int>(total);
                emit syncProgressChanged();
            });
    connect(m_controller, &RepoController::authFailed, this,
            [this](QString) { emit authRequired(); });
    connect(m_controller, &RepoController::mergeStateChanged, this,
            [this](const gittide::MergeState& s) { m_merge = s; emit mergeStateChanged(); });
    connect(m_controller, &RepoController::mergeFinished, this,
            [this](const QString&) { /* refresh driven by the controller cascade */ });
    connect(m_controller, &RepoController::rebaseStateChanged, this,
            [this](const gittide::RebaseState& s) { m_rebase = s; emit rebaseStateChanged(); });
    connect(m_controller, &RepoController::rebaseFinished, this,
            [this](const QString&) { /* refresh driven by the controller cascade */ });
    connect(m_controller, &RepoController::commitMessageReady,
            this, &RepoViewModel::commitMessageReady);
    connect(m_controller, &RepoController::rebaseTodoReady,
            this, &RepoViewModel::rebaseTodoReady);
    connect(m_controller, &RepoController::gitDirRefreshed,
            this, &RepoViewModel::repoStructureChanged);
}

bool RepoViewModel::repoOpen() const
{
    return m_open;
}

QString RepoViewModel::repoPath() const
{
    return m_open ? m_controller->path() : QString();
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

HistoryListModel* RepoViewModel::graph() const
{
    return m_graph;
}

void RepoViewModel::open(const QString& path)
{
    m_headOid.clear();
    m_lastLayout     = {};
    updateReorderableRun();
    m_headArrived    = false;
    m_historyArrived = false;
    m_controller->open(path);
    m_open = true;
    emit changed();
    QCoro::connect(m_controller->refreshStatus(), this, [] {});
    QCoro::connect(m_controller->refreshBranches(), this, [] {});
    QCoro::connect(m_controller->refreshHistory(), this, [] {});
    QCoro::connect(m_controller->refreshSyncStatus(), this, [] {});
}

void RepoViewModel::close()
{
    m_open = false;
    m_files->setFiles({});
    m_sel.clear();
    m_activeFile.clear();
    m_diff->clear();
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    m_selectedCommit.clear();
    m_activeCommitFile.clear();
    m_branches->setBranches({});
    m_history->setLayout({}, {});
    m_graph->setLayout({}, {});
    m_branch.clear();
    m_headBranch.clear();
    m_headOid.clear();
    m_lastLayout     = {};
    updateReorderableRun();
    m_headArrived    = false;
    m_historyArrived = false;
    m_sync           = {};
    m_syncing        = false;
    m_pendingOp      = PendingOp::None;
    m_sessionCred    = {};
    m_merge          = {};
    m_mergeStartName.clear();
    m_rebase         = {};
    emit changed();
    emit branchChanged();
    emit activeFileChanged();
    emit checkedChanged();
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    emit syncStatusChanged();
    emit mergeStateChanged();
    emit rebaseStateChanged();
}

void RepoViewModel::selectFile(const QString& path)
{
    m_activeFile = path;
    emit activeFileChanged();

    // When the selected file is conflicted, load its raw marker-bearing content
    // for inline resolution rather than computing a normal diff (D29).
    const auto isConflicted = [&]() -> bool
    {
        for (const auto& cp : m_merge.conflictedPaths)
        {
            if (pathToQString(cp) == path)
                return true;
        }
        return false;
    };

    if (!path.isEmpty() && isConflicted())
    {
        const QString raw = m_controller->readWorkingFile(path);
        m_diff->setConflictContent(raw);
        return;
    }

    QCoro::connect(m_controller->refreshDiff(path, gittide::DiffTarget::WorktreeVsHead), this, [] {});
}

void RepoViewModel::selectFileAtRow(int row)
{
    if (row < 0 || row >= m_files->rowCount())
        return;
    selectFile(m_files->pathAt(row));
}

void RepoViewModel::selectCommitAtRow(int row)
{
    if (!m_history || row < 0 || row >= m_history->rowCount())
        return;
    const QString oid = m_history->data(m_history->index(row, 0),
                                        HistoryListModel::OidRole).toString();
    selectCommit(oid);
}

void RepoViewModel::selectGraphCommitAtRow(int row)
{
    if (!m_graph || row < 0 || row >= m_graph->rowCount())
        return;
    const QString oid = m_graph->data(m_graph->index(row, 0),
                                      HistoryListModel::OidRole).toString();
    selectCommit(oid);
}

void RepoViewModel::selectCommitFileAtRow(int row)
{
    if (row < 0 || row >= m_commitFiles->rowCount())
        return;
    selectCommitFile(m_commitFiles->pathAt(row));
}

void RepoViewModel::acceptConflict(int region, int which)
{
    QString out;
    switch (which)
    {
        case 0:  out = m_diff->acceptCurrent(region);  break;
        case 1:  out = m_diff->acceptIncoming(region); break;
        default: out = m_diff->acceptBoth(region);     break;
    }
    m_controller->writeWorkingFile(m_activeFile, out);
    selectFile(m_activeFile);                          // re-loads content / diff
    QCoro::connect(m_controller->refreshStatus(), this, [] {}); // re-derives MergeState (D30)
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

void RepoViewModel::setBlockChecked(int row, bool checked)
{
    // Routes through DiffLinesModel; covered lines emit lineToggled() → onLineToggled().
    m_diff->setBlockChecked(row, checked);
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
        // A submodule always stages whole-file: git records its HEAD (last commit),
        // never the dirty working content, so the superproject can't pin a "-dirty"
        // pointer. The dirty/partial UI state is a warning only — never line-staging.
        if (m_files->isSubmodule(row))
        {
            selections.push_back(gittide::StageSelection{.path = fsPath, .hunkIndex = std::nullopt, .lineIndices = {}});
        }
        else if (rowState == ChangedFilesModel::Partial)
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

void RepoViewModel::refreshGraph()
{
    QCoro::connect(m_controller->refreshGraph(), this, [] {});
}

void RepoViewModel::resync()
{
    if (!m_open)
        return;
    QCoro::connect(m_controller->refreshAll(), this, [] {});
}

void RepoViewModel::onHistory(const gittide::GraphLayout& layout)
{
    m_lastLayout     = layout;
    m_historyArrived = true;
    applyHistoryIfReady();
}

void RepoViewModel::onGraph(const gittide::GraphLayout& layout)
{
    m_graph->setLayout(layout, m_headOid);
}

void RepoViewModel::onRefTips(const QHash<QString, QStringList>& oidToLabels)
{
    m_graph->setRefTips(oidToLabels);
}

void RepoViewModel::applyHistoryIfReady()
{
    // Apply only once both signals for this open() have landed; whichever arrives
    // last triggers the single setLayout. Works for empty/unborn repos too
    // (empty layout + empty oid → model reset to zero rows).
    if (m_headArrived && m_historyArrived)
    {
        m_history->setLayout(m_lastLayout, m_headOid);
        updateReorderableRun();
    }
}

void RepoViewModel::updateReorderableRun()
{
    // Count newest commits (from HEAD down) that have exactly one parent. A merge
    // (≥2 parents) or the root (0 parents) ends the run: neither can be replayed
    // by a simple cherry-pick reorder. Need ≥2 to reorder anything.
    int run = 0;
    for (const auto& row : m_lastLayout.rows)
    {
        if (row.commit.parents.size() == 1)
            ++run;
        else
            break;
    }
    const int next = run >= 2 ? run : 0;
    if (next != m_reorderableRunLength)
    {
        m_reorderableRunLength = next;
        emit reorderableRunChanged();
    }
}

void RepoViewModel::reorderCommits(int fromRow, int toRow, const QString& band)
{
    const int n = m_reorderableRunLength;
    if (n < 2 || fromRow == toRow)
        return;
    if (fromRow < 0 || fromRow >= n || toRow < 0 || toRow >= n)
        return;

    // Build the run newest-first, then re-insert the dragged commit.
    QList<QString> run;
    run.reserve(n);
    for (int i = 0; i < n; ++i)
        run << QString::fromStdString(m_lastLayout.rows[i].commit.oid);

    // Detach onto the parent of the run's (original) oldest commit.
    const auto& deepest = m_lastLayout.rows[n - 1].commit;
    if (deepest.parents.empty())
        return; // defensive: run members always have one parent
    const QString base = QString::fromStdString(deepest.parents[0]);

    // Insert the dragged commit on the chosen side of the target. This list is
    // newest-first, so "above" (newer) is the lower index and "below" (older) the
    // higher one; resolve the target's index *after* removing the dragged commit.
    const QString dragged = run[fromRow];
    const QString target  = run[toRow];
    run.removeAt(fromRow);
    const int t = run.indexOf(target);
    run.insert(band == QStringLiteral("above") ? t : t + 1, dragged);

    // The engine wants the plan oldest-first, all picks (pure reorder).
    QStringList oids, actions;
    for (auto it = run.rbegin(); it != run.rend(); ++it)
    {
        oids << *it;
        actions << QStringLiteral("pick");
    }
    QCoro::connect(m_controller->startInteractiveRebase(base, actions, oids), this, [] {});
}

void RepoViewModel::squashCommitInto(int fromRow, int toRow)
{
    const int n = m_reorderableRunLength;
    if (n < 2 || fromRow == toRow)
        return;
    if (fromRow < 0 || fromRow >= n || toRow < 0 || toRow >= n)
        return;

    // Build the run newest-first (index 0 == HEAD), same as reorderCommits.
    QList<QString> run;
    run.reserve(n);
    for (int i = 0; i < n; ++i)
        run << QString::fromStdString(m_lastLayout.rows[i].commit.oid);

    // Detach onto the parent of the run's (original) oldest commit.
    const auto& deepest = m_lastLayout.rows[n - 1].commit;
    if (deepest.parents.empty())
        return; // defensive: run members always have one parent
    const QString base = QString::fromStdString(deepest.parents[0]);

    const QString dragged = run[fromRow];
    const QString target  = run[toRow];

    // Place the dragged commit immediately *newer-adjacent* to the target, then
    // mark it squash. The engine reads the plan oldest-first and folds a squash
    // entry into the preceding kept commit; placing dragged directly after target
    // in oldest-first order folds dragged into target. In this newest-first list,
    // "directly after target oldest-first" == "directly before target here", i.e.
    // insert at the target's index so dragged sits one slot newer than target.
    run.removeAt(fromRow);
    const int t = run.indexOf(target);
    run.insert(t, dragged);

    // Hand the engine the plan oldest-first: all picks, except the dragged commit
    // is squash.
    QStringList oids, actions;
    for (auto it = run.rbegin(); it != run.rend(); ++it)
    {
        oids << *it;
        actions << (*it == dragged ? QStringLiteral("squash") : QStringLiteral("pick"));
    }
    QCoro::connect(m_controller->startInteractiveRebase(base, actions, oids), this, [] {});
}

void RepoViewModel::onStatus(const std::vector<gittide::FileStatus>& files)
{
    // Preserve the user's per-file selection across refreshes. The live watcher
    // fires status often, so blindly re-checking everything would keep undoing the
    // user's unchecks. Rule the user asked for: a refresh never silently grows a
    // partial selection — only when *everything* was checked do new files/changes
    // come in checked; otherwise new entries arrive unchecked. Files already known
    // keep their prior state (and any per-line map).
    const std::map<QString, FileSel> prev = m_sel;
    bool prevAllChecked = !prev.empty();
    for (const auto& [path, fs] : prev)
    {
        if (fs.state != ChangedFilesModel::Checked)
        {
            prevAllChecked = false;
            break;
        }
    }
    // First load (no prior selection) defaults to checked, matching a fresh open.
    const bool defaultChecked = prev.empty() || prevAllChecked;

    m_files->setFiles(files); // rebuilds rows, each defaulting to Checked

    m_sel.clear();
    for (int row = 0; row < m_files->rowCount(QModelIndex()); ++row)
    {
        const QString path = m_files->pathAt(row);
        const auto    it   = prev.find(path);
        FileSel       fs   = (it != prev.end())
                                 ? it->second
                                 : FileSel{defaultChecked ? ChangedFilesModel::Checked
                                                          : ChangedFilesModel::Unchecked,
                                           {}};
        m_sel[path] = fs;
        // setFiles defaulted the row to Checked; only push a non-Checked state.
        if (fs.state != ChangedFilesModel::Checked)
            m_files->setCheckState(row, fs.state);
    }

    // I-1 fix: When a file is selected (e.g. mid-conflict resolution), preserve
    // it if it still appears in the refreshed file list — clearing unconditionally
    // blanks the diff panel after every acceptConflict(). Only clear when the
    // previously active file is gone from the new list (deleted, staged, etc.).
    const bool fileStillPresent = !m_activeFile.isEmpty()
        && std::any_of(files.begin(), files.end(),
                       [&](const gittide::FileStatus& f)
                       { return pathToQString(f.path) == m_activeFile; });
    if (fileStillPresent)
    {
        // Re-load content so the diff reflects the updated on-disk state (e.g.
        // after one conflict region was resolved). selectFile is idempotent here.
        selectFile(m_activeFile);
    }
    else
    {
        m_activeFile.clear();
        m_diff->clear();
        emit activeFileChanged();
    }

    emit checkedChanged();
}

void RepoViewModel::onDiff(const QString& path, const gittide::DiffResult& result)
{
    if (path != m_activeFile)
        return;
    const FileSel& fs = m_sel[path];
    m_diff->setDiff(result, fs.checkedLinesByHunk, fs.state == ChangedFilesModel::Checked, /*blocks=*/true);
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

    // Build oid → local-branch-name map so the History pane can offer
    // "Merge into <current>" on rows that are branch tips.
    QHash<QString, QString> tips;
    for (const auto& b : branches)
    {
        if (b.kind == gittide::BranchKind::Local && !b.tipOid.empty())
            tips.insert(QString::fromStdString(b.tipOid), QString::fromStdString(b.name));
    }
    m_history->setLocalBranchTips(tips);
    m_graph->setLocalBranchTips(tips);

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
    m_rangeOld.clear();
    m_rangeNew.clear();
    m_detailHeader.clear();
    m_detailHint.clear();
    emit historyDetailChanged();
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
    if (!m_rangeOld.isEmpty())
        QCoro::connect(m_controller->refreshRangeDiff(m_rangeOld, m_rangeNew, path), this, [] {});
    else
        QCoro::connect(m_controller->refreshCommitDiff(m_selectedCommit, path), this, [] {});
}

void RepoViewModel::checkoutCommit(const QString& oid)
{
    QCoro::connect(m_controller->checkoutCommit(oid), this, [] {});
}

void RepoViewModel::rewordHead(const QString& message)
{
    QCoro::connect(m_controller->rewordHead(message), this, [] {});
}

void RepoViewModel::undoLastCommit()
{
    QCoro::connect(m_controller->undoLastCommit(), this, [] {});
}

void RepoViewModel::requestCommitMessage(const QString& oid)
{
    QCoro::connect(m_controller->requestCommitMessage(oid), this, [] {});
}

void RepoViewModel::onCommitFiles(const QString& oid, const std::vector<gittide::FileStatus>& files)
{
    if (oid != m_selectedCommit)
        return;
    m_commitFiles->setFiles(files);
    // Auto-select the first file so its diff loads without an extra click.
    if (!files.empty())
        selectCommitFile(m_commitFiles->pathAt(0));
}

void RepoViewModel::onCommitDiff(const QString& oid, const QString& path, const gittide::DiffResult& result)
{
    if (oid != m_selectedCommit || path != m_activeCommitFile)
        return;
    // Read-only: no checked lines, not whole-file-checked. The QML detail view
    // hides the per-line checkbox column.
    m_commitDiff->setDiff(result, {}, false);
}

void RepoViewModel::selectCommitRows(const QVariantList& rows)
{
    if (!m_history)
        return;

    // Sort row indices ascending; drop out-of-range entries.
    std::vector<int> idx;
    idx.reserve(rows.size());
    for (const auto& v : rows)
    {
        bool ok = false;
        const int r = v.toInt(&ok);
        if (ok && r >= 0 && r < m_history->rowCount())
            idx.push_back(r);
    }
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    if (idx.empty())
        return;

    auto oidAt = [&](int row) {
        return m_history->data(m_history->index(row, 0),
                               HistoryListModel::OidRole).toString();
    };

    if (idx.size() == 1)
    {
        selectCommit(oidAt(idx.front())); // single-commit flow; clears range mode
        return;
    }

    const bool contiguous = (idx.back() - idx.front()) == int(idx.size()) - 1;
    if (!contiguous)
    {
        applyRangeHint();
        return;
    }

    // History is newest-first: the largest row index is the oldest commit.
    applyRange(oidAt(idx.back()), oidAt(idx.front()), int(idx.size()));
}

void RepoViewModel::applyRange(const QString& oldOid, const QString& newOid, int count)
{
    m_rangeOld = oldOid;
    m_rangeNew = newOid;
    m_selectedCommit = newOid;          // anchor for per-file diff matching
    m_activeCommitFile.clear();
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    m_detailHint.clear();
    m_detailHeader = QStringLiteral("Changes across %1 commits (%2…%3)")
                         .arg(count)
                         .arg(oldOid.left(7), newOid.left(7));
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    emit historyDetailChanged();
    QCoro::connect(m_controller->refreshRangeFiles(oldOid, newOid), this, [] {});
}

void RepoViewModel::applyRangeHint()
{
    m_rangeOld.clear();
    m_rangeNew.clear();
    m_selectedCommit.clear();
    m_activeCommitFile.clear();
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    m_detailHeader.clear();
    m_detailHint = QStringLiteral("Select a contiguous range to see combined changes.");
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    emit historyDetailChanged();
}

void RepoViewModel::onRangeFiles(const QString& oldOid, const QString& newOid,
                                 const std::vector<gittide::FileStatus>& files)
{
    if (oldOid != m_rangeOld || newOid != m_rangeNew)
        return;
    m_commitFiles->setFiles(files);
    // Auto-select the first file so its diff loads without an extra click.
    if (!files.empty())
        selectCommitFile(m_commitFiles->pathAt(0));
}

void RepoViewModel::onRangeDiff(const QString& oldOid, const QString& newOid,
                                const QString& path, const gittide::DiffResult& result)
{
    if (oldOid != m_rangeOld || newOid != m_rangeNew || path != m_activeCommitFile)
        return;
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

void RepoViewModel::applyPullDefault(bool rebase)
{
    m_pullRebase = rebase;
    emit pullRebaseChanged();
}

void RepoViewModel::startMerge(const QString& name)
{
    m_mergeStartName = name;
    QCoro::connect(m_controller->merge(name), this, [] {});
}

void RepoViewModel::commitMerge(const QString& message)
{
    QCoro::connect(m_controller->commitMerge(gittide::CommitRequest{message.toStdString()}), this, [] {});
}

void RepoViewModel::abortMerge()
{
    QCoro::connect(m_controller->abortMerge(), this, [] {});
}

void RepoViewModel::startRebase(const QString& ref)
{
    QCoro::connect(m_controller->startRebase(ref), this, [] {});
}

void RepoViewModel::startInteractiveRebase(QString base, QStringList actions, QStringList oids)
{
    QCoro::connect(m_controller->startInteractiveRebase(base, actions, oids), this, [] {});
}

void RepoViewModel::requestSquashTodo(const QVariantList& rows)
{
    QStringList oids;
    for (const auto& r : rows)
    {
        const int row = r.toInt();
        if (!m_history || row < 0 || row >= m_history->rowCount())
            continue;
        oids << m_history->data(m_history->index(row, 0), HistoryListModel::OidRole).toString();
    }
    QCoro::connect(m_controller->buildSquashTodo(oids), this, [] {});
}

void RepoViewModel::requestRebaseTodo(QString fromOid)
{
    QCoro::connect(m_controller->buildRebaseTodo(fromOid), this, [] {});
}

void RepoViewModel::continueRebase(const QString& message)
{
    QCoro::connect(m_controller->continueRebase(message), this, [] {});
}

void RepoViewModel::skipRebase()
{
    QCoro::connect(m_controller->skipRebase(), this, [] {});
}

void RepoViewModel::abortRebase()
{
    QCoro::connect(m_controller->abortRebase(), this, [] {});
}

void RepoViewModel::retryMergeDeinitSubmodules()
{
    // I-2 fix: m_mergeStartName is only set when GitTide initiated the merge in
    // this session. For merges started on the CLI or across restarts it is empty.
    // Fall back to the disk-derived ref name from MERGE_MSG (D30) rather than
    // passing an empty string — which would abort-then-fail destructively.
    QString name = m_mergeStartName;
    if (name.isEmpty())
        name = mergedRef(); // parsed from MERGE_MSG, never fabricated in-memory

    // If both are empty we cannot determine the target; do nothing. The controller
    // has its own guard too, but we avoid even starting the operation here.
    if (name.isEmpty())
    {
        emit operationFailed(
            tr("Cannot determine which branch to merge; resolve or abort this merge manually."));
        return;
    }

    QCoro::connect(m_controller->retryMergeDeinitSubmodules(name), this, [] {});
}

void RepoViewModel::discardFile(const QString& path)
{
    gittide::StageSelection sel{
        .path        = qstringToPath(path),
        .hunkIndex   = std::nullopt,
        .lineIndices = {}
    };
    QCoro::connect(m_controller->discard(sel), this, [] {});
}

void RepoViewModel::openInEditor(const QString& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void RepoViewModel::revealInFileManager(const QString& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void RepoViewModel::copyToClipboard(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
}

} // namespace gittide::ui
