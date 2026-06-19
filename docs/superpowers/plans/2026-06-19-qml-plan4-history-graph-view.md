# QML History View + Commit Graph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the QML History-tab placeholder with a real commit-history view — a virtualized commit list, a multi-color lane graph drawn per row, gravatar/initials avatars, and a read-only "selected commit → files → diff" detail pane with a checkout action.

**Architecture:** The backend is already shipped and tested: `core/GraphBuilder` produces lane data; `RepoController` exposes `refreshHistory → historyReady(GraphLayout)`, `refreshCommitFiles → commitFilesReady`, `refreshCommitDiff → commitDiffReady`, and `checkoutCommit`. This plan adds (1) a focused QML list model `HistoryListModel`, (2) a `GraphColumn` `QQuickPaintedItem` that ports `GraphDelegate::paint` to the scene-graph and colors lanes from `theme.laneColors`, (3) history wiring on `RepoViewModel` (the QML façade), and (4) QML views (`HistoryPane`, `CommitDetail`, `Avatar`) wired into `WorkingPane`'s History tab. `core/` and the controller layer are untouched.

**Tech Stack:** C++23, Qt 6 (Quick / QuickControls2 Basic / Qml), QCoro, Qt Test (C++ harness loading QML via `QQmlApplicationEngine`).

## Global Constraints

- **No Qt in `core/`** — all new code lives in `ui/`. `core/graph.hpp` structs (`GraphRow`/`CommitNode`) are plain C++ and **cannot** be introspected from QML; any field QML needs is surfaced as a model role (C++ extracts `std::string → QString`) or unpacked inside a C++ `QQuickPaintedItem`. Never expose a core struct as a QML-readable gadget.
- **Colour comes from a theme token, never a hex literal** — QML reads `theme.<token>`; the lane palette is `theme.laneColors` (a `QVariantList`, the one documented multi-hue exception). The **HEAD node is white** (`theme.head`). Models/items never hardcode colour.
- **Paths via `generic_u8string()`** — use `pathToQString` / `qstringToPath` from `gittide/ui/metatypes.hpp` at every path boundary; never `.string()`.
- **Errors are values in core** — but this layer only consumes already-emitted Qt signals; surface failures via the existing `RepoViewModel::operationFailed(QString)`.
- **TDD** — failing test first. New `ui/` sources go in `ui/CMakeLists.txt`; new tests go in the matching list in `tests/CMakeLists.txt`.
- **Code style** — `m_` members, lowercase file names, Allman braces (`.clang-format`), Doxygen comments next to declarations. Split a rename from content changes (N/A here — all new files).
- **Build / test** — `cmake --build build --parallel`; `ctest --test-dir build --output-on-failure`. Single test exe: `./build/tests/gittide_ui_tests` (the target the QML tests link into — confirm exact name from `tests/CMakeLists.txt`).
- **QML graph rendering primitive decision (design §6 open question):** resolved here as **`QQuickPaintedItem`** — it ports `GraphDelegate::paint` (a `QPainter` routine) almost verbatim, keeping one source of truth for the lane geometry.

---

### Task 1: `HistoryListModel` — QML list model for commit rows

**Files:**
- Create: `ui/include/gittide/ui/historylistmodel.hpp`
- Create: `ui/src/historylistmodel.cpp`
- Modify: `ui/CMakeLists.txt` (add the two paths to the `gittide_ui` source list, next to `historymodel.cpp`)
- Test: `tests/ui/test_qml_history.cpp` (new file)
- Modify: `tests/CMakeLists.txt` (add `test_qml_history.cpp` to the same list that holds `ui/test_qml_shell.cpp`, around line 60)

**Interfaces:**
- Consumes: `gittide::GraphLayout`, `gittide::GraphRow`, `gittide::CommitNode` (`core/include/gittide/graph.hpp`); `Q_DECLARE_METATYPE(gittide::GraphRow)` (already in `metatypes.hpp`).
- Produces: class `gittide::ui::HistoryListModel : public QAbstractListModel` with:
  - `enum Roles { GraphRole = Qt::UserRole + 1, SummaryRole, AuthorRole, DateRole, OidRole, ShortOidRole, IsHeadRole };`
  - role names (QML-facing): `graphRow`, `summary`, `author`, `date`, `oid`, `shortOid`, `isHead`.
  - `void setLayout(const gittide::GraphLayout& layout, const QString& headOid);`
  - `Q_PROPERTY(int laneCount READ laneCount NOTIFY changed)` + `int laneCount() const;`
  - `signals: void changed();`

- [ ] **Step 1: Write the failing test** (`tests/ui/test_qml_history.cpp`)

```cpp
#include <QtTest>
#include <QAbstractItemModel>

#include "gittide/graph.hpp"
#include "gittide/ui/historylistmodel.hpp"

using namespace gittide::ui;

namespace {
gittide::GraphLayout twoRowLayout()
{
    // Row 0: child (HEAD) at lane 0 with one out-edge to its parent (row 1).
    // Row 1: parent at lane 0, initial commit (no out-edges, line from above).
    gittide::GraphRow head;
    head.commit.oid     = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    head.commit.summary = "second";
    head.commit.author  = "Ada";
    head.commit.time    = 0;
    head.commit.lane    = 0;
    head.lineFromAbove  = false;
    head.outEdges       = {gittide::GraphEdge{0, 0}};

    gittide::GraphRow base;
    base.commit.oid     = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    base.commit.summary = "first";
    base.commit.author  = "Ada";
    base.commit.time    = 0;
    base.commit.lane    = 0;
    base.lineFromAbove  = true;

    gittide::GraphLayout layout;
    layout.rows      = {head, base};
    layout.laneCount = 1;
    return layout;
}
}

class TestQmlHistory : public QObject
{
    Q_OBJECT
private slots:
    void model_exposes_history_rows_via_roles()
    {
        HistoryListModel model;
        model.setLayout(twoRowLayout(), QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

        QCOMPARE(model.rowCount(QModelIndex()), 2);
        QCOMPARE(model.laneCount(), 1);

        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.data(top, HistoryListModel::SummaryRole).toString(), QStringLiteral("second"));
        QCOMPARE(model.data(top, HistoryListModel::AuthorRole).toString(), QStringLiteral("Ada"));
        QCOMPARE(model.data(top, HistoryListModel::ShortOidRole).toString(), QStringLiteral("aaaaaaa"));
        QCOMPARE(model.data(top, HistoryListModel::IsHeadRole).toBool(), true);
        QVERIFY(model.data(top, HistoryListModel::GraphRole).canConvert<gittide::GraphRow>());

        const QModelIndex bottom = model.index(1, 0);
        QCOMPARE(model.data(bottom, HistoryListModel::IsHeadRole).toBool(), false);

        // QML role names are present and spelled as the delegates expect.
        const auto names = model.roleNames();
        QCOMPARE(names.value(HistoryListModel::SummaryRole), QByteArrayLiteral("summary"));
        QCOMPARE(names.value(HistoryListModel::GraphRole), QByteArrayLiteral("graphRow"));
        QCOMPARE(names.value(HistoryListModel::IsHeadRole), QByteArrayLiteral("isHead"));
    }
};

#include "test_qml_history.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel` then `ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: FAIL — compile error, `historylistmodel.hpp` not found.

- [ ] **Step 3: Write the header** (`ui/include/gittide/ui/historylistmodel.hpp`)

```cpp
#pragma once
#include <QAbstractListModel>
#include <QString>

#include "gittide/graph.hpp"

namespace gittide::ui {

/// QML list model backing the History tab. One row per GraphRow from a
/// GraphLayout (Plan 5a / GraphBuilder). Unlike the QWidget-era HistoryModel
/// (a table model painted by GraphDelegate), this is a single-column list whose
/// roles feed a QML ListView delegate directly: graphRow carries the GraphRow
/// for GraphColumn to paint; the rest are pre-formatted display strings. Knows
/// nothing about colour — the delegate maps lane index → theme.laneColors.
class HistoryListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int laneCount READ laneCount NOTIFY changed)
public:
    enum Roles
    {
        GraphRole = Qt::UserRole + 1, // QVariant<gittide::GraphRow>
        SummaryRole,
        AuthorRole,
        DateRole,     // pre-formatted "yyyy-MM-dd hh:mm"
        OidRole,      // full 40-char SHA
        ShortOidRole, // first 7 chars
        IsHeadRole,   // true when oid == the layout's HEAD oid
    };

    using QAbstractListModel::QAbstractListModel;

    /// Replace all rows. headOid is the full SHA of HEAD; the matching row's
    /// IsHeadRole is true (drives the white HEAD node in the graph).
    void setLayout(const gittide::GraphLayout& layout, const QString& headOid);

    int laneCount() const
    {
        return m_layout.laneCount;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void changed();

private:
    gittide::GraphLayout m_layout;
    QString              m_headOid;
};

} // namespace gittide::ui
```

- [ ] **Step 4: Write the implementation** (`ui/src/historylistmodel.cpp`)

```cpp
#include "gittide/ui/historylistmodel.hpp"

#include <QDateTime>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

void HistoryListModel::setLayout(const gittide::GraphLayout& layout, const QString& headOid)
{
    beginResetModel();
    m_layout = layout;
    m_headOid = headOid;
    endResetModel();
    emit changed();
}

int HistoryListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_layout.rows.size());
}

QVariant HistoryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= m_layout.rows.size())
        return {};
    const auto& gr  = m_layout.rows[row];
    const QString oid = QString::fromStdString(gr.commit.oid);

    switch (role)
    {
    case GraphRole:
        return QVariant::fromValue(gr);
    case SummaryRole:
        return QString::fromStdString(gr.commit.summary);
    case AuthorRole:
        return QString::fromStdString(gr.commit.author);
    case DateRole:
        return QDateTime::fromSecsSinceEpoch(gr.commit.time).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    case OidRole:
        return oid;
    case ShortOidRole:
        return oid.left(7);
    case IsHeadRole:
        return !m_headOid.isEmpty() && oid == m_headOid;
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryListModel::roleNames() const
{
    return {
        {GraphRole, QByteArrayLiteral("graphRow")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {AuthorRole, QByteArrayLiteral("author")},
        {DateRole, QByteArrayLiteral("date")},
        {OidRole, QByteArrayLiteral("oid")},
        {ShortOidRole, QByteArrayLiteral("shortOid")},
        {IsHeadRole, QByteArrayLiteral("isHead")},
    };
}

} // namespace gittide::ui
```

- [ ] **Step 5: Register sources & test in CMake**

In `ui/CMakeLists.txt`, after the `historymodel.cpp`/`.hpp` pair (lines 39-40) add:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/historylistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/historylistmodel.hpp
```

In `tests/CMakeLists.txt`, in the list that contains `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_shell.cpp`, add a line:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_history.cpp
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/historylistmodel.hpp ui/src/historylistmodel.cpp ui/CMakeLists.txt tests/ui/test_qml_history.cpp tests/CMakeLists.txt
git commit -m "feat(ui): HistoryListModel — QML list model for commit rows"
```

---

### Task 2: `GraphColumn` — `QQuickPaintedItem` that draws one commit row's lanes

**Files:**
- Create: `ui/include/gittide/ui/graphcolumn.hpp`
- Create: `ui/src/graphcolumn.cpp`
- Modify: `ui/CMakeLists.txt` (add the two paths next to the Task 1 additions)
- Test: `tests/ui/test_qml_history.cpp` (add a slot)

**Interfaces:**
- Consumes: `gittide::GraphRow`, `gittide::GraphEdge` (`graph.hpp`); `GraphDelegate::kLaneWidth` (16) and `kDotRadius` (4) geometry — re-declared as `GraphColumn` constants to keep this item independent of the QWidget delegate.
- Produces: class `gittide::ui::GraphColumn : public QQuickPaintedItem`, QML name `GraphColumn` in module `GitTide 1.0`, with properties:
  - `QVariant graphRow` (WRITE `setGraphRow`, unpacked to `gittide::GraphRow` in C++)
  - `QVariantList laneColors` (WRITE `setLaneColors`) — list of `QColor`
  - `QColor headColor` (WRITE `setHeadColor`)
  - `int laneCount` (WRITE `setLaneCount`) — drives `implicitWidth = laneCount * kLaneWidth`
  - `bool head` (WRITE `setHead`)
  - free function `void registerQmlTypes();` declared in `qmlcontext.hpp` (added in this task), which calls `qmlRegisterType<GraphColumn>("GitTide", 1, 0, "GraphColumn")`.

- [ ] **Step 1: Write the failing test** (add this slot to `TestQmlHistory` in `tests/ui/test_qml_history.cpp`, and add the include `#include "gittide/ui/graphcolumn.hpp"` and `#include "gittide/ui/qmlcontext.hpp"` at the top)

```cpp
    void graph_column_unpacks_row_and_sizes_to_lane_count()
    {
        GraphColumn item;

        gittide::GraphRow gr;
        gr.commit.oid  = "cccccccccccccccccccccccccccccccccccccccc";
        gr.commit.lane = 1;
        item.setGraphRow(QVariant::fromValue(gr));
        item.setLaneCount(3);

        QCOMPARE(item.laneCount(), 3);
        // implicitWidth tracks lane count so the ListView can reserve a fixed gutter.
        QCOMPARE(item.implicitWidth(), qreal(3 * GraphColumn::kLaneWidth));

        // The QML type is registered under the GitTide module.
        registerQmlTypes();
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData("import GitTide 1.0\nGraphColumn { laneCount: 2 }", QUrl());
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QCOMPARE(obj->property("laneCount").toInt(), 2);
    }
```

Add at the top of the file: `#include <QQmlEngine>`, `#include <QQmlComponent>`, `#include <memory>`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `graphcolumn.hpp` not found.

- [ ] **Step 3: Write the header** (`ui/include/gittide/ui/graphcolumn.hpp`)

```cpp
#pragma once
#include <QColor>
#include <QQuickPaintedItem>
#include <QVariant>
#include <QVariantList>

#include "gittide/graph.hpp"

namespace gittide::ui {

/// Scene-graph item that paints ONE commit row's lane graph: pass-through
/// verticals, the incoming line from above, the commit dot, and outgoing edges
/// down to parent lanes. Ports GraphDelegate::paint (the QWidget delegate) to
/// Qt Quick. Each lane is drawn in laneColors[lane % count]; the dot is the
/// commit's lane colour unless `head` is set, in which case it is headColor
/// (white). Used inside the History ListView delegate, one instance per row.
class GraphColumn : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariant graphRow READ graphRow WRITE setGraphRow NOTIFY changed)
    Q_PROPERTY(QVariantList laneColors READ laneColors WRITE setLaneColors NOTIFY changed)
    Q_PROPERTY(QColor headColor READ headColor WRITE setHeadColor NOTIFY changed)
    Q_PROPERTY(int laneCount READ laneCount WRITE setLaneCount NOTIFY changed)
    Q_PROPERTY(bool head READ head WRITE setHead NOTIFY changed)
public:
    static constexpr int kLaneWidth = 16;
    static constexpr int kDotRadius = 4;

    explicit GraphColumn(QQuickItem* parent = nullptr);

    QVariant graphRow() const { return m_graphRow; }
    QVariantList laneColors() const { return m_laneColors; }
    QColor headColor() const { return m_headColor; }
    int laneCount() const { return m_laneCount; }
    bool head() const { return m_head; }

    void setGraphRow(const QVariant& row);
    void setLaneColors(const QVariantList& colors);
    void setHeadColor(const QColor& color);
    void setLaneCount(int count);
    void setHead(bool head);

    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    QColor laneColor(int lane) const;
    static int laneX(int lane)
    {
        return lane * kLaneWidth + kLaneWidth / 2;
    }

    QVariant         m_graphRow;
    QVariantList     m_laneColors;
    QColor           m_headColor = Qt::white;
    int              m_laneCount = 1;
    bool             m_head      = false;
};

} // namespace gittide::ui
```

- [ ] **Step 4: Write the implementation** (`ui/src/graphcolumn.cpp`)

```cpp
#include "gittide/ui/graphcolumn.hpp"

#include <QPainter>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

GraphColumn::GraphColumn(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    qRegisterMetaType<gittide::GraphRow>();
    setImplicitWidth(m_laneCount * kLaneWidth);
}

void GraphColumn::setGraphRow(const QVariant& row)
{
    m_graphRow = row;
    emit changed();
    update();
}

void GraphColumn::setLaneColors(const QVariantList& colors)
{
    m_laneColors = colors;
    emit changed();
    update();
}

void GraphColumn::setHeadColor(const QColor& color)
{
    m_headColor = color;
    emit changed();
    update();
}

void GraphColumn::setLaneCount(int count)
{
    m_laneCount = count < 1 ? 1 : count;
    setImplicitWidth(m_laneCount * kLaneWidth);
    emit changed();
    update();
}

void GraphColumn::setHead(bool head)
{
    m_head = head;
    emit changed();
    update();
}

QColor GraphColumn::laneColor(int lane) const
{
    if (m_laneColors.isEmpty())
        return Qt::gray;
    return m_laneColors.at(lane % m_laneColors.size()).value<QColor>();
}

void GraphColumn::paint(QPainter* painter)
{
    if (!m_graphRow.canConvert<gittide::GraphRow>())
        return;
    const auto row = m_graphRow.value<gittide::GraphRow>();

    painter->setRenderHint(QPainter::Antialiasing, true);

    const int top = 0;
    const int bot = static_cast<int>(height());
    const int mid = bot / 2;
    const int cx  = laneX(row.commit.lane);

    auto pen = [&](int lane)
    {
        painter->setPen(QPen(laneColor(lane), 1.5));
    };

    // 1. Pass-through verticals span the full cell, each in its own lane colour.
    for (int lane : row.passThroughs)
    {
        pen(lane);
        painter->drawLine(laneX(lane), top, laneX(lane), bot);
    }

    // 2. Incoming line to the circle (top half), in the commit's lane colour.
    if (row.lineFromAbove)
    {
        pen(row.commit.lane);
        painter->drawLine(cx, top, cx, mid);
    }

    // 3. Outgoing edges to parent lanes (bottom half), coloured by destination lane.
    for (const auto& e : row.outEdges)
    {
        pen(e.toLane);
        painter->drawLine(laneX(e.fromLane), mid, laneX(e.toLane), bot);
    }

    // 4. Commit dot — lane colour, or white for HEAD.
    const QColor dot = m_head ? m_headColor : laneColor(row.commit.lane);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot);
    painter->drawEllipse(QPoint(cx, mid), kDotRadius, kDotRadius);
}

} // namespace gittide::ui
```

- [ ] **Step 5: Add the QML type registration** — append to `ui/include/gittide/ui/qmlcontext.hpp` (inside `namespace gittide::ui`, after the existing `installQmlContext` declaration):

```cpp
/// Register C++ types exposed to QML (currently GraphColumn in module
/// "GitTide" 1.0). Idempotent — safe to call once per process or per engine.
void registerQmlTypes();
```

Append to `ui/src/qmlcontext.cpp`: add `#include <QtQml>` and `#include "gittide/ui/graphcolumn.hpp"` to the includes, then inside `namespace gittide::ui`:

```cpp
void registerQmlTypes()
{
    qmlRegisterType<GraphColumn>("GitTide", 1, 0, "GraphColumn");
}
```

And call it at the very top of `installQmlContext(...)` (first line of the body) so every engine — app and tests — has the type:

```cpp
    registerQmlTypes();
```

- [ ] **Step 6: Register sources in CMake** — in `ui/CMakeLists.txt`, after the Task 1 additions:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graphcolumn.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/graphcolumn.hpp
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: PASS (both slots).

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/graphcolumn.hpp ui/src/graphcolumn.cpp ui/include/gittide/ui/qmlcontext.hpp ui/src/qmlcontext.cpp ui/CMakeLists.txt tests/ui/test_qml_history.cpp
git commit -m "feat(ui): GraphColumn QQuickPaintedItem — per-row multi-color lane graph"
```

---

### Task 3: `RepoViewModel` — history model, refresh, HEAD oid

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_qml_history.cpp` (add a slot)

**Interfaces:**
- Consumes: `RepoController::refreshHistory(unsigned)`, signal `historyReady(gittide::GraphLayout)`, signal `headChanged(gittide::HeadState)` (already connected to `onHead`); `HistoryListModel` (Task 1); `pathToQString` (`metatypes.hpp`); `QCoro::connect`.
- Produces (new on `RepoViewModel`):
  - `Q_PROPERTY(gittide::ui::HistoryListModel* history READ history CONSTANT)`
  - `HistoryListModel* history() const;`
  - `Q_INVOKABLE void refreshHistory();`
  - members `HistoryListModel* m_history`, `QString m_headOid`.
  - private slot `void onHistory(const gittide::GraphLayout& layout);`

- [ ] **Step 1: Write the failing test** (add slot to `TestQmlHistory`; reuse the `make_dirty_repo` helper pattern — copy the helper from `test_qml_shell.cpp` into a `qml_history_test` namespace at the top of `test_qml_history.cpp`, OR `#include` is not possible so duplicate the ~35-line helper verbatim under a new namespace name to avoid ODR clash)

```cpp
    void history_model_populates_after_open()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        QVERIFY(vm.history()->rowCount(QModelIndex()) >= 1);
        // Top row is HEAD (the "init" commit) — IsHeadRole true.
        const QModelIndex top = vm.history()->index(0, 0);
        QCOMPARE(vm.history()->data(top, HistoryListModel::IsHeadRole).toBool(), true);

        std::filesystem::remove_all(dir);
    }
```

Add `#include "gittide/ui/repoviewmodel.hpp"`, `#include <QSignalSpy>`, and the git/test includes (`<git2.h>`, `<QRandomGenerator>`, `<filesystem>`, `<fstream>`) plus the duplicated `make_dirty_repo` under `namespace qml_history_test { ... }`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `vm.history()` does not exist (no member `history`).

- [ ] **Step 3: Edit the header** (`ui/include/gittide/ui/repoviewmodel.hpp`)

Add the include near the other model includes:

```cpp
#include "gittide/ui/historylistmodel.hpp"
```

Add the property next to the other `Q_PROPERTY` lines:

```cpp
    Q_PROPERTY(gittide::ui::HistoryListModel* history READ history CONSTANT)
```

Add the getter declaration next to `branches()`:

```cpp
    HistoryListModel* history() const;
```

Add the invokable next to the other `Q_INVOKABLE` declarations:

```cpp
    Q_INVOKABLE void refreshHistory();
```

Add the private slot declaration next to `onBranches`:

```cpp
    void onHistory(const gittide::GraphLayout& layout);
```

Add the members next to `m_branches` and `m_branch`:

```cpp
    HistoryListModel* m_history = nullptr;
    QString           m_headOid;
```

- [ ] **Step 4: Edit the implementation** (`ui/src/repoviewmodel.cpp`)

In the constructor initializer list, add (after `m_branches(...)`):

```cpp
    , m_history(new HistoryListModel(this))
```

In the constructor body, add the connection (after the `branchesChanged` connect):

```cpp
    connect(m_controller, &RepoController::historyReady, this, &RepoViewModel::onHistory);
```

Add the getter (next to `branches()`):

```cpp
HistoryListModel* RepoViewModel::history() const
{
    return m_history;
}
```

Add the invokable + slot (anywhere among the method definitions):

```cpp
void RepoViewModel::refreshHistory()
{
    QCoro::connect(m_controller->refreshHistory(), this, [] {});
}

void RepoViewModel::onHistory(const gittide::GraphLayout& layout)
{
    m_history->setLayout(layout, m_headOid);
}
```

In `onHead(...)`, capture the HEAD oid so `IsHeadRole` is correct. Add near the top of the function body, before the `label` logic:

```cpp
    m_headOid = QString::fromStdString(head.oid);
```

In `open(...)`, kick a history refresh alongside status/branches:

```cpp
    QCoro::connect(m_controller->refreshHistory(), this, [] {});
```

Note: `headChanged` fires during `refreshStatus`/`refreshBranches`, so `m_headOid` is set before or close to `historyReady`. If a race leaves the first layout without a HEAD flag, the next `refreshHistory()` (e.g. after a commit) re-applies it; acceptable for now. (Carry-forward note in Outcome.)

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp tests/ui/test_qml_history.cpp
git commit -m "feat(ui): RepoViewModel exposes history model + refreshHistory + HEAD oid"
```

---

### Task 4: `Avatar.qml` + `HistoryPane.qml` — commit list with graph & avatars

**Files:**
- Create: `ui/qml/Avatar.qml`
- Create: `ui/qml/HistoryPane.qml`
- Modify: `ui/qml/WorkingPane.qml` (replace the `historyTabBody` placeholder body with `HistoryPane`)
- Modify: `ui/qml/qml.qrc` (add `Avatar.qml`, `HistoryPane.qml`)
- Test: `tests/ui/test_qml_history.cpp` (add a slot loading `Main.qml`)

**Interfaces:**
- Consumes: context properties `repoVm` (`RepoViewModel`) and `theme` (`QmlTheme`, exposes `laneColors`, `head`, surface/text tokens); `repoVm.history` (`HistoryListModel`) with roles `graphRow`, `summary`, `author`, `date`, `shortOid`, `isHead`, `oid`; `GraphColumn` from `import GitTide 1.0`.
- Produces: `HistoryPane` with `objectName: "historyPane"`; the commit `ListView` has `objectName: "historyList"`; each row exposes the GraphColumn. `Avatar` takes a `name` string property.

- [ ] **Step 1: Write the failing test** (add slot to `TestQmlHistory`)

```cpp
    void history_list_binds_to_history_model()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* list = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("model").value<QAbstractItemModel*>(), vm.history());

        std::filesystem::remove_all(dir);
    }
```

Add includes at the top of the file if not already present: `#include <QQmlApplicationEngine>`, `#include <QQmlContext>`, `#include "gittide/ui/qmltheme.hpp"`, `#include "gittide/ui/thememanager.hpp"`, `#include "gittide/ui/repolistmodel.hpp"`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: FAIL — `findChild("historyList")` returns null (History tab is still the placeholder).

- [ ] **Step 3: Write `ui/qml/Avatar.qml`**

```qml
import QtQuick

// Circular avatar. Gravatar-by-email is a future enhancement (CommitNode carries
// no email yet), so this renders initials on an accent-tinted disc — the design's
// "initials fallback now" path.
Rectangle {
    id: avatar
    property string name: ""
    implicitWidth: 24
    implicitHeight: 24
    radius: width / 2
    color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)

    Label {
        anchors.centerIn: parent
        text: {
            var parts = avatar.name.trim().split(/\s+/).filter(function (p) { return p.length > 0 })
            if (parts.length === 0) return "?"
            if (parts.length === 1) return parts[0].charAt(0).toUpperCase()
            return (parts[0].charAt(0) + parts[parts.length - 1].charAt(0)).toUpperCase()
        }
        color: theme.accent
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
```

- [ ] **Step 4: Write `ui/qml/HistoryPane.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GitTide 1.0

RowLayout {
    id: historyPane
    objectName: "historyPane"
    spacing: 0

    // ---- Commit list (graph + avatar + summary/author/date) ----
    ListView {
        id: historyList
        objectName: "historyList"
        Layout.preferredWidth: 420
        Layout.fillHeight: true
        clip: true
        model: repoVm ? repoVm.history : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 48
            color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

            // Accent left border on the selected row (over the graph cell, x=0).
            Rectangle {
                visible: parent.ListView.isCurrentItem
                width: 2
                height: parent.height
                color: theme.accent
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    historyList.currentIndex = index
                    if (repoVm) repoVm.selectCommit(model.oid)
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 12
                spacing: 8

                GraphColumn {
                    Layout.fillHeight: true
                    Layout.preferredWidth: implicitWidth
                    graphRow: model.graphRow
                    laneColors: theme.laneColors
                    headColor: theme.head
                    laneCount: repoVm && repoVm.history ? repoVm.history.laneCount : 1
                    head: model.isHead
                }

                Avatar {
                    name: model.author
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: model.summary
                        color: theme.textPrimary
                        font.pixelSize: 13
                    }
                    RowLayout {
                        spacing: 8
                        Label {
                            text: model.author
                            color: theme.textMuted
                            font.pixelSize: 11
                        }
                        Label {
                            text: model.shortOid
                            color: theme.textMuted
                            font.family: "monospace"
                            font.pixelSize: 11
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            text: model.date
                            color: theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }

    // Hairline divider
    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        color: theme.border
    }

    // ---- Selected-commit detail (files + read-only diff) ----
    CommitDetail {
        objectName: "commitDetail"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
```

> `CommitDetail` is created in Task 5. To keep this task's test green before then, also create a minimal placeholder `ui/qml/CommitDetail.qml` now and flesh it out in Task 5:
> ```qml
> import QtQuick
> Item { id: commitDetail }
> ```
> Add `CommitDetail.qml` to `qml.qrc` in this task as well.

- [ ] **Step 5: Wire into `WorkingPane.qml`** — replace the placeholder `Item { objectName: "historyTabBody" ... }` (lines 52-60) with:

```qml
        // Index 1: History — commit list + graph + read-only commit detail.
        HistoryPane {
            objectName: "historyTabBody"
        }
```

- [ ] **Step 6: Register QML files** — add to `ui/qml/qml.qrc` inside `<qresource>`:

```xml
    <file>Avatar.qml</file>
    <file>HistoryPane.qml</file>
    <file>CommitDetail.qml</file>
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: PASS — `historyList` found and bound to `vm.history()`.

- [ ] **Step 8: Commit**

```bash
git add ui/qml/Avatar.qml ui/qml/HistoryPane.qml ui/qml/CommitDetail.qml ui/qml/WorkingPane.qml ui/qml/qml.qrc tests/ui/test_qml_history.cpp
git commit -m "feat(ui): QML History pane — commit list with multi-color graph + avatars"
```

---

### Task 5: Commit selection → read-only files + diff (`CommitDetail.qml`)

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Modify: `ui/qml/CommitDetail.qml` (flesh out the placeholder)
- Test: `tests/ui/test_qml_history.cpp` (add a slot)

**Interfaces:**
- Consumes: `RepoController::refreshCommitFiles(QString oid)` → `commitFilesReady(QString oid, std::vector<gittide::FileStatus>)`; `refreshCommitDiff(QString oid, QString path)` → `commitDiffReady(QString oid, QString path, gittide::DiffResult)`; existing models `ChangedFilesModel` (read-only use — ignore checkboxes) and `DiffLinesModel` (`setDiff(result, {}, false)` → all unchecked, read-only); `DiffLinesModel` roles `lineKind`,`oldNo`,`newNo`,`lineText`.
- Produces (new on `RepoViewModel`):
  - `Q_PROPERTY(gittide::ui::ChangedFilesModel* commitFiles READ commitFiles CONSTANT)`
  - `Q_PROPERTY(gittide::ui::DiffLinesModel* commitDiff READ commitDiff CONSTANT)`
  - `Q_PROPERTY(QString selectedCommit READ selectedCommit NOTIFY selectedCommitChanged)`
  - `Q_PROPERTY(QString activeCommitFile READ activeCommitFile NOTIFY activeCommitFileChanged)`
  - `Q_INVOKABLE void selectCommit(const QString& oid);`
  - `Q_INVOKABLE void selectCommitFile(const QString& path);`
  - signals `selectedCommitChanged()`, `activeCommitFileChanged()`.
  - slots `onCommitFiles(QString, std::vector<gittide::FileStatus>)`, `onCommitDiff(QString, QString, gittide::DiffResult)`.
  - members `ChangedFilesModel* m_commitFiles`, `DiffLinesModel* m_commitDiff`, `QString m_selectedCommit`, `QString m_activeCommitFile`.

- [ ] **Step 1: Write the failing test** (add slot to `TestQmlHistory`)

```cpp
    void selecting_a_commit_loads_its_files_and_diff()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        // Select the HEAD commit (row 0) by its oid.
        const QString oid = vm.history()->data(vm.history()->index(0, 0), HistoryListModel::OidRole).toString();
        QSignalSpy filesSpy(vm.commitFiles(), &QAbstractItemModel::modelReset);
        vm.selectCommit(oid);
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.selectedCommit(), oid);
        QVERIFY(vm.commitFiles()->rowCount(QModelIndex()) >= 1);

        // Select the first file → its read-only diff loads.
        const QString path = vm.commitFiles()->pathAt(0);
        QSignalSpy diffSpy(vm.commitDiff(), &QAbstractItemModel::modelReset);
        vm.selectCommitFile(path);
        QVERIFY(diffSpy.wait(3000));
        QCOMPARE(vm.activeCommitFile(), path);
        QVERIFY(vm.commitDiff()->rowCount(QModelIndex()) >= 1);

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `vm.commitFiles()` / `selectCommit` do not exist.

- [ ] **Step 3: Edit the header** (`ui/include/gittide/ui/repoviewmodel.hpp`)

Add properties (next to the other `Q_PROPERTY` lines):

```cpp
    Q_PROPERTY(gittide::ui::ChangedFilesModel* commitFiles READ commitFiles CONSTANT)
    Q_PROPERTY(gittide::ui::DiffLinesModel* commitDiff READ commitDiff CONSTANT)
    Q_PROPERTY(QString selectedCommit READ selectedCommit NOTIFY selectedCommitChanged)
    Q_PROPERTY(QString activeCommitFile READ activeCommitFile NOTIFY activeCommitFileChanged)
```

Add getters:

```cpp
    ChangedFilesModel* commitFiles() const;
    DiffLinesModel* commitDiff() const;
    QString selectedCommit() const;
    QString activeCommitFile() const;
```

Add invokables:

```cpp
    Q_INVOKABLE void selectCommit(const QString& oid);
    Q_INVOKABLE void selectCommitFile(const QString& path);
```

Add signals:

```cpp
    void selectedCommitChanged();
    void activeCommitFileChanged();
```

Add slots (next to `onHistory`):

```cpp
    void onCommitFiles(const QString& oid, const std::vector<gittide::FileStatus>& files);
    void onCommitDiff(const QString& oid, const QString& path, const gittide::DiffResult& result);
```

Add members:

```cpp
    ChangedFilesModel* m_commitFiles = nullptr;
    DiffLinesModel*    m_commitDiff  = nullptr;
    QString            m_selectedCommit;
    QString            m_activeCommitFile;
```

- [ ] **Step 4: Edit the implementation** (`ui/src/repoviewmodel.cpp`)

Constructor initializer list (after `m_history(...)`):

```cpp
    , m_commitFiles(new ChangedFilesModel(this))
    , m_commitDiff(new DiffLinesModel(this))
```

Constructor body connections:

```cpp
    connect(m_controller, &RepoController::commitFilesReady, this, &RepoViewModel::onCommitFiles);
    connect(m_controller, &RepoController::commitDiffReady, this, &RepoViewModel::onCommitDiff);
```

Getters + invokables + slots:

```cpp
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
```

- [ ] **Step 5: Flesh out `ui/qml/CommitDetail.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: commitDetail
    spacing: 0

    // Header: selected commit short-oid (empty when nothing selected).
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        Label {
            Layout.fillWidth: true
            text: repoVm && repoVm.selectedCommit.length > 0
                  ? ("Commit " + repoVm.selectedCommit.substring(0, 7))
                  : "Select a commit"
            color: theme.textSecondary
            font.family: "monospace"
            font.pixelSize: 12
        }
    }

    // ---- Files in the commit (read-only) ----
    ListView {
        id: commitFilesList
        objectName: "commitFilesList"
        Layout.fillWidth: true
        Layout.preferredHeight: 160
        clip: true
        model: repoVm ? repoVm.commitFiles : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 28
            color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    commitFilesList.currentIndex = index
                    if (repoVm) repoVm.selectCommitFile(model.filePath)
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.family: "monospace"
                    font.pixelSize: 12
                    textFormat: Text.RichText
                    text: "<font color='" + theme.textMuted + "'>" + model.fileDir + "</font>"
                          + "<font color='" + theme.textPrimary + "'>" + model.fileName + "</font>"
                }
                Label {
                    text: model.statusLetter
                    font.family: "monospace"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: model.statusKind === "added" ? theme.stateAdded
                           : model.statusKind === "deleted" ? theme.stateDeleted
                           : model.statusKind === "untracked" ? theme.stateUntracked
                           : theme.stateModified
                }
            }
        }
    }

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

    // ---- Read-only diff (no per-line checkboxes) ----
    ListView {
        id: commitDiffList
        objectName: "commitDiffList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: repoVm ? repoVm.commitDiff : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 20
            color: model.lineKind === "added" ? Qt.rgba(theme.stateAdded.r, theme.stateAdded.g, theme.stateAdded.b, 0.12)
                   : model.lineKind === "removed" ? Qt.rgba(theme.stateDeleted.r, theme.stateDeleted.g, theme.stateDeleted.b, 0.12)
                   : model.lineKind === "hunk" ? theme.surfaceOverlay
                   : "transparent"

            RowLayout {
                anchors.fill: parent
                spacing: 6
                Label {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: theme.textMuted
                    text: model.lineKind === "hunk" ? ""
                          : (model.oldNo > 0 ? model.oldNo : "") + " " + (model.newNo > 0 ? model.newNo : "")
                }
                Label {
                    Layout.preferredWidth: 10
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: model.lineKind === "added" ? "+" : model.lineKind === "removed" ? "−" : ""
                    color: model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textMuted
                }
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    text: model.lineText
                    color: model.lineKind === "hunk" ? theme.textMuted
                           : model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textPrimary
                }
            }
        }
    }
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp ui/qml/CommitDetail.qml tests/ui/test_qml_history.cpp
git commit -m "feat(ui): commit selection loads read-only files + diff in History detail"
```

---

### Task 6: Checkout a commit from the detail pane

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Modify: `ui/qml/CommitDetail.qml` (add a checkout button to the header)
- Test: `tests/ui/test_qml_history.cpp` (add a slot)

**Interfaces:**
- Consumes: `RepoController::checkoutCommit(QString oid)` → `QCoro::Task<void>` (detaches HEAD at the commit); on success the controller re-emits `statusChanged`/`headChanged`/`historyReady`, refreshing the views automatically.
- Produces: `Q_INVOKABLE void checkoutCommit(const QString& oid);` on `RepoViewModel`.

- [ ] **Step 1: Write the failing test** (add slot to `TestQmlHistory`)

```cpp
    void checkout_commit_detaches_head_at_that_commit()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        const QString oid = vm.history()->data(vm.history()->index(0, 0), HistoryListModel::OidRole).toString();
        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
        vm.checkoutCommit(oid);
        QVERIFY(branchSpy.wait(3000));
        // Detached HEAD label is "detached @ <short>".
        QVERIFY(vm.currentBranch().startsWith(QStringLiteral("detached @ ")));

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `vm.checkoutCommit` does not exist.

- [ ] **Step 3: Edit the header** — add the invokable next to `selectCommit`:

```cpp
    Q_INVOKABLE void checkoutCommit(const QString& oid);
```

- [ ] **Step 4: Edit the implementation** — add:

```cpp
void RepoViewModel::checkoutCommit(const QString& oid)
{
    QCoro::connect(m_controller->checkoutCommit(oid), this, [] {});
}
```

- [ ] **Step 5: Add the checkout button** to `ui/qml/CommitDetail.qml` header `RowLayout` (after the commit-oid Label):

```qml
        Button {
            objectName: "checkoutCommitButton"
            visible: repoVm && repoVm.selectedCommit.length > 0
            text: "Checkout"
            contentItem: Label {
                text: parent.text
                color: theme.textPrimary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 6
                color: parent.hovered ? theme.surfaceOverlay : "transparent"
                border.color: theme.border
                border.width: 1
            }
            onClicked: if (repoVm) repoVm.checkoutCommit(repoVm.selectedCommit)
        }
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R QmlHistory --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Run the FULL suite to confirm no regressions**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests PASS (the prior suite + the new `QmlHistory` cases).

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp ui/qml/CommitDetail.qml tests/ui/test_qml_history.cpp
git commit -m "feat(ui): checkout commit from History detail pane"
```

---

### Task 7: Documentation — spec, plan outcome, index

**Files:**
- Modify: `docs/spec/design/design.md` (note the QML History view: per-row `GraphColumn`, lane palette via `theme.laneColors`, white HEAD node, selected-row accent border)
- Modify: `docs/spec/product/product.md` (History screen now implemented in QML: commit list + graph + avatars + read-only commit detail + checkout)
- Modify: `docs/superpowers/plans/2026-06-19-qml-plan4-history-graph-view.md` (fill the Outcome section below)
- Modify: `docs/plans/index.md` (add a row for this plan if QML plans are tracked there; otherwise skip — confirm by reading the table first)

**Interfaces:** none (docs only).

- [ ] **Step 1: Update the design spec** — under the QML migration / components section, add a short subsection documenting: `GraphColumn` is a `QQuickPaintedItem` painting one `GraphRow`; lanes coloured by `lane % theme.laneColors.length`; HEAD dot = `theme.head` (white); History row selection shows an accent left border over the graph cell. Keep symbol-level detail in the Doxygen comments, not here.

- [ ] **Step 2: Update the product spec** — mark the History tab as shipped in QML; describe the three-part detail flow (commit → files → read-only diff) and the checkout action.

- [ ] **Step 3: Fill the Outcome section** of this plan (see template at the bottom).

- [ ] **Step 4: Verify the plan index** — read `docs/plans/index.md`; if QML plans are listed there, add a row; if QML plans live only under `docs/superpowers/plans/`, leave it and note that in the Outcome.

- [ ] **Step 5: Commit**

```bash
git add docs/
git commit -m "docs: QML History view + commit graph (Plan 4 outcome + spec)"
```

---

## Outcome

- Shipped:
  - `HistoryListModel` — QML list model; roles `graphRow/summary/author/date/oid/shortOid/isHead`; `laneCount` property.
  - `GraphColumn` (`QQuickPaintedItem`, registered as `GitTide 1.0/GraphColumn`) — ports `GraphDelegate::paint` to Qt Quick; lane colour = `laneColors[lane % count]`; HEAD dot = `headColor` (white); `implicitWidth` = `laneCount × 16`.
  - `RepoViewModel` extended: `history`, `refreshHistory`, `commitFiles`, `commitDiff`, `selectedCommit`, `activeCommitFile`, `selectCommit`, `selectCommitFile`, `checkoutCommit`; arrival-flag reconciliation (`m_headArrived`/`m_historyArrived` + `applyHistoryIfReady()`) for correct HEAD marking and clean reopen.
  - `Avatar.qml` — initials on accent-tinted disc (gravatar deferred; `CommitNode` carries no email).
  - `HistoryPane.qml` — virtualized commit `ListView` with graph + avatar + summary/author/date; selected row = `surfaceOverlay` fill + 2px `accent` border at `x=0`.
  - `CommitDetail.qml` — read-only commit-detail pane: changed-files list → diff; **Checkout** button (detaches HEAD).
  - `WorkingPane.qml` History tab — replaced "coming soon" placeholder with `HistoryPane`.
  - `tests/ui/test_qml_history.cpp` (`TestQmlHistory`) — 7 test slots; full suite 78/78 green.
- Spec updated: `docs/spec/design/design.md` §Components → "QML History view"; `docs/spec/product/product.md` §History tab.
- Code: `ui/{historylistmodel,graphcolumn,repoviewmodel,qmlcontext}.*`, `ui/qml/{Avatar,HistoryPane,CommitDetail,WorkingPane}.qml`, `tests/ui/test_qml_history.cpp`.
- Commits: ab29a93..8ea678c (Tasks 1–6 + fixes).
- Plan index (`docs/plans/index.md`): QML-migration plans (Plans 3a/3b/3c/4) live exclusively under `docs/superpowers/plans/` and are not listed in the main index — consistent with the precedent set by earlier QML plans. No row added.
- Carry-forward Minors:
  - `oid` computed unconditionally in `HistoryListModel::data` for all roles (T1).
  - `DateRole`/`OidRole` not asserted in tests (T1).
  - Redundant `qRegisterMetaType<GraphRow>` in `GraphColumn` ctor; no painter save/restore (T2).
  - `graph.hpp` pulled into public `repoviewmodel.hpp` header via `m_lastLayout` value member — widens include surface, harmless (T3).
  - 8px graph gap vs. accent border (design §3.2 cosmetic — `RowLayout.leftMargin` shifts lanes to `x=8` not `x=2`; check at live demo) (T4).
  - `"monospace"` font literal (consistent with `ChangesPane`; extract when mono-font token added) (T4).
  - `selectCommit` does not eagerly clear `m_commitFiles` — old file list lingers until async arrives (diff clears immediately, asymmetric UX) *** best-value fix (T5).
  - No per-method Doxygen on `checkoutCommit` (consistent with sibling invokables — pre-existing pattern) (T6).
  - HEAD-oid/history-ready arrival race re-applied on next `refreshHistory()` (acceptable; arrival flags handle steady state).

---

## Self-Review

**1. Spec coverage (against `2026-06-19-qml-ui-migration-design.md`):**
- §2 "Commit graph — QML `QQuickPaintedItem` drawing lanes from GraphBuilder" → Task 2 (`GraphColumn`). ✓
- §3.1 multi-color lane palette + white HEAD node → Task 2 (`laneColor`, `headColor`/`head`). ✓
- §3.2 selected history row highlights the whole row incl. graph cell, accent left border from x=0 → Task 4 (delegate `isCurrentItem` fill + 2px accent rect at x=0). ✓
- §3.5 gravatar avatars, initials fallback now → Task 4 (`Avatar.qml`, initials; gravatar deferred — CommitNode lacks email, noted). ✓ (fallback path)
- Read-only history diff (selected commit → files → diff), from existing controller API (Plan 9a/9b) → Task 5. ✓
- Checkout from history → Task 6. ✓
- §6 open question "graph primitive (Canvas vs QQuickPaintedItem vs Shapes)" → resolved to `QQuickPaintedItem` in Global Constraints + Task 2. ✓
- **Out of scope (correctly):** §3.3 submodules in repo tree (sidebar, separate plan); §4 manifest project (needs pugixml, deferred); §3.6/3.7 branch bar/dropdown (Plan 3b, done); light-theme mockup (token swap already supported by `QmlTheme`).

**2. Placeholder scan:** No "TBD"/"add error handling"/"similar to Task N" — every code step shows full code. The only intentional stub is the minimal `CommitDetail.qml` in Task 4, explicitly fleshed out in Task 5.

**3. Type consistency:** `HistoryListModel::{GraphRole,SummaryRole,AuthorRole,DateRole,OidRole,ShortOidRole,IsHeadRole}` and role names (`graphRow`,`summary`,`author`,`date`,`oid`,`shortOid`,`isHead`) are identical across Tasks 1/3/4/5/6. `GraphColumn` properties (`graphRow`,`laneColors`,`headColor`,`laneCount`,`head`) match between Task 2 (def) and Task 4 (use). `RepoViewModel` additions (`history`,`commitFiles`,`commitDiff`,`selectedCommit`,`activeCommitFile`,`selectCommit`,`selectCommitFile`,`checkoutCommit`,`refreshHistory`) are consistent across Tasks 3/5/6 and the QML in Tasks 4/5/6. Controller signal/method names (`historyReady`,`commitFilesReady`,`commitDiffReady`,`refreshHistory`,`refreshCommitFiles`,`refreshCommitDiff`,`checkoutCommit`) verified against `ui/include/gittide/ui/repocontroller.hpp`.
