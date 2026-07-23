# QCoro adds co_await support over QFuture; it is built from source via FetchContent.
# Trim QCoro to the modules we use; the rest pull in Qt components we don't link.
# IMPORTANT: QCoro must be made available BEFORE ECM (Extra CMake Modules) is added
# to CMAKE_MODULE_PATH. ECM 6.5.0 removed the INTERFACE keyword from
# ecm_generate_pri_file; QCoro 0.11.0 uses that keyword and would fail if it found
# ECM 6.5.0 on the module path instead of its own bundled ECM cmake files. The
# dependencies.cmake include order (qcoro before syntax_highlighting) guarantees this.
set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QML OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTDBUS OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTNETWORK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG        v0.11.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(qcoro)
