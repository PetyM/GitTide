#pragma once
#include <QMetaType>
#include <vector>
#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"

// Core types carried across Qt signals / captured by QSignalSpy. Q_DECLARE_METATYPE
// must appear at global scope. Call qRegisterMetaType<T>() once per type before use
// (the emitting classes do this in their constructors).
Q_DECLARE_METATYPE(gitgui::StageSelection)
Q_DECLARE_METATYPE(gitgui::DiffResult)
Q_DECLARE_METATYPE(gitgui::CommitRequest)
Q_DECLARE_METATYPE(std::vector<gitgui::FileStatus>)
