#pragma once
#include <QMetaType>
#include <vector>
#include "gittide/Diff.hpp"
#include "gittide/FileStatus.hpp"
#include "gittide/Graph.hpp"

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
