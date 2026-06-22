#pragma once
#include <QMetaType>
#include <QString>
#include <filesystem>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"

// Core types carried across Qt signals / captured by QSignalSpy. Q_DECLARE_METATYPE
// must appear at global scope. Call qRegisterMetaType<T>() once per type before use
// (the emitting classes do this in their constructors).
Q_DECLARE_METATYPE(gittide::StageSelection)

namespace gittide::ui {

/// Convert a filesystem path to a QString using UTF-8 (via generic_u8string).
/// This is the canonical path→QString conversion for all UI boundary code; it
/// matches what ChangedFilesList stores in PathRole so cross-component lookups
/// (m_sel keys in ChangesView, lineCheckToggled in DiffView) are byte-identical
/// for any path, including non-ASCII filenames on Windows/MSVC.
inline QString pathToQString(const std::filesystem::path& p)
{
    const auto u8 = p.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(u8.data()),
                             static_cast<qsizetype>(u8.size()));
}

/// Convert a QString that holds a UTF-8 path back to std::filesystem::path.
/// Inverse of pathToQString — use whenever a QString from a Qt signal must be
/// passed to core/libgit2 as a path.
inline std::filesystem::path qstringToPath(const QString& qs)
{
    const QByteArray utf8 = qs.toUtf8();
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.constData()),
                      static_cast<std::size_t>(utf8.size())));
}

} // namespace gittide::ui
Q_DECLARE_METATYPE(std::vector<gittide::StageSelection>)
Q_DECLARE_METATYPE(gittide::DiffResult)
Q_DECLARE_METATYPE(gittide::CommitRequest)
Q_DECLARE_METATYPE(std::vector<gittide::FileStatus>)
Q_DECLARE_METATYPE(gittide::DiffTarget)
Q_DECLARE_METATYPE(gittide::GraphLayout)
Q_DECLARE_METATYPE(gittide::GraphRow)
Q_DECLARE_METATYPE(gittide::BranchInfo)
Q_DECLARE_METATYPE(std::vector<gittide::BranchInfo>)
Q_DECLARE_METATYPE(gittide::HeadState)
Q_DECLARE_METATYPE(gittide::MergeState)
