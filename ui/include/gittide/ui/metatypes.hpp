#pragma once
#include <QMetaType>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"

// Core types carried across Qt signals / captured by QSignalSpy. Q_DECLARE_METATYPE
// must appear at global scope. Call qRegisterMetaType<T>() once per type before use
// (the emitting classes do this in their constructors).
Q_DECLARE_METATYPE(gittide::StageSelection)
Q_DECLARE_METATYPE(gittide::DiffResult)
Q_DECLARE_METATYPE(gittide::CommitRequest)
Q_DECLARE_METATYPE(std::vector<gittide::FileStatus>)
Q_DECLARE_METATYPE(gittide::DiffTarget)
Q_DECLARE_METATYPE(gittide::GraphLayout)
Q_DECLARE_METATYPE(gittide::GraphRow)
Q_DECLARE_METATYPE(gittide::BranchInfo)
Q_DECLARE_METATYPE(std::vector<gittide::BranchInfo>)
Q_DECLARE_METATYPE(gittide::HeadState)
