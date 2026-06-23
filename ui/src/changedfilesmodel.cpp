#include "gittide/ui/changedfilesmodel.hpp"

#include <algorithm>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

QString ChangedFilesModel::letterForFlags(gittide::StatusFlag flags)
{
    using F = gittide::StatusFlag;
    if (gittide::hasFlag(flags, F::Conflicted))
        return QStringLiteral("C");
    if (gittide::hasFlag(flags, F::IndexNew))
        return QStringLiteral("A");
    if (gittide::hasFlag(flags, F::IndexModified))
        return QStringLiteral("M");
    if (gittide::hasFlag(flags, F::IndexDeleted))
        return QStringLiteral("D");
    if (gittide::hasFlag(flags, F::WtNew))
        return QStringLiteral("U");
    if (gittide::hasFlag(flags, F::WtModified))
        return QStringLiteral("M");
    if (gittide::hasFlag(flags, F::WtDeleted))
        return QStringLiteral("D");
    return QStringLiteral("?");
}

QString ChangedFilesModel::kindForFlags(gittide::StatusFlag flags)
{
    using F = gittide::StatusFlag;
    if (gittide::hasFlag(flags, F::Conflicted))
        return QStringLiteral("conflict");
    if (gittide::hasFlag(flags, F::IndexNew))
        return QStringLiteral("added");
    if (gittide::hasFlag(flags, F::IndexModified))
        return QStringLiteral("modified");
    if (gittide::hasFlag(flags, F::IndexDeleted))
        return QStringLiteral("deleted");
    if (gittide::hasFlag(flags, F::WtNew))
        return QStringLiteral("untracked");
    if (gittide::hasFlag(flags, F::WtModified))
        return QStringLiteral("modified");
    if (gittide::hasFlag(flags, F::WtDeleted))
        return QStringLiteral("deleted");
    return QStringLiteral("modified");
}

int ChangedFilesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ChangedFilesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case DirRole:
        return r.dir;
    case NameRole:
        return r.name;
    case PathRole:
        return r.path;
    case LetterRole:
        return r.letter;
    case KindRole:
        return r.kind;
    case CheckRole:
        return static_cast<int>(r.check);
    default:
        return {};
    }
}

QHash<int, QByteArray> ChangedFilesModel::roleNames() const
{
    return {
        {DirRole, "fileDir"},
        {NameRole, "fileName"},
        {PathRole, "filePath"},
        {LetterRole, "statusLetter"},
        {KindRole, "statusKind"},
        {CheckRole, "checkState"},
    };
}

void ChangedFilesModel::setFiles(const std::vector<gittide::FileStatus>& files)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(files.size());
    for (const auto& f : files)
    {
        const QString full = pathToQString(f.path);
        const int slash    = full.lastIndexOf(QLatin1Char('/'));
        Row r;
        r.dir    = slash >= 0 ? full.left(slash + 1) : QString();
        r.name   = slash >= 0 ? full.mid(slash + 1) : full;
        r.path   = full;
        r.letter = letterForFlags(f.flags);
        r.kind   = kindForFlags(f.flags);
        r.check  = Checked;
        r.flags  = f.flags;
        m_rows.push_back(std::move(r));
    }
    // Sort conflicted rows to the top, stable sort preserves relative order otherwise
    std::stable_sort(m_rows.begin(), m_rows.end(), [](const Row& a, const Row& b) {
        const bool a_conflicted = gittide::hasFlag(a.flags, gittide::StatusFlag::Conflicted);
        const bool b_conflicted = gittide::hasFlag(b.flags, gittide::StatusFlag::Conflicted);
        return a_conflicted > b_conflicted;
    });
    endResetModel();
}

void ChangedFilesModel::setChecked(int row, bool checked)
{
    setCheckState(row, checked ? Checked : Unchecked);
}

void ChangedFilesModel::setCheckState(int row, Check state)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    if (m_rows[static_cast<std::size_t>(row)].check == state)
        return;
    m_rows[static_cast<std::size_t>(row)].check = state;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {CheckRole});
}

ChangedFilesModel::Check ChangedFilesModel::checkState(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return Unchecked;
    return m_rows[static_cast<std::size_t>(row)].check;
}

QString ChangedFilesModel::pathAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};
    return m_rows[static_cast<std::size_t>(row)].path;
}

int ChangedFilesModel::rowForPath(const QString& path) const
{
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        if (m_rows[static_cast<std::size_t>(i)].path == path)
            return i;
    return -1;
}

int ChangedFilesModel::checkedCount() const
{
    int n = 0;
    for (const auto& r : m_rows)
        if (r.check != Unchecked)
            ++n;
    return n;
}

} // namespace gittide::ui
