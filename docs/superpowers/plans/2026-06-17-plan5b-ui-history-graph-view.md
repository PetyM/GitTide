# Plan 5b — UI: HistoryModel + GraphDelegate + HistoryView + Wiring

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire Plan 5a's Core graph layer into the UI — async commit log fetching, a table model backed by `GraphLayout`, a custom delegate that paints the lane graph, and a `HistoryView` widget wired into MainWindow's History tab.

**Architecture:** `AsyncRepo` gains a `log()` method. `RepoController` gets a `refreshHistory()` coroutine slot that emits `historyReady(GraphLayout)`. `HistoryModel` is a `QAbstractTableModel` (4 columns: graph, summary, author, date). `GraphDelegate` overrides `paint()` for column 0. `HistoryView` is a `QWidget` containing a `QTableView` with the model and delegate. `MainWindow` replaces the "History — Plan 4" placeholder label with `HistoryView` and triggers `refreshHistory` when a repo is selected. Qt's model/view architecture virtualizes rendering — only visible rows call `paint()`, so 100k commits never stall the UI.

**Tech Stack:** C++23, Qt6 Widgets, QCoro 0.11, libgit2 (via `GitRepo`/`AsyncRepo`), Catch2/QtTest.

## Global Constraints

- All UI code in `ui/`; Core (`core/`) is Qt-free.
- Coroutine slots: all args **by value** (survives `co_await` suspension).
- Fire-and-forget `QCoro::Task` anchored via `QCoro::connect(task, this, []{})`.
- `AsyncRepo` lock rule: hold scoped_lock inside the lambda passed to `QtConcurrent::run`.
- `GraphLayout` must be `Q_DECLARE_METATYPE`'d in `Metatypes.hpp` and registered before use.
- Build: `cmake --build /home/michal/Documents/gitgui/build -j`. UI tests: `ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure`.
- Adding a UI test = edit BOTH `tests/CMakeLists.txt` (source list) AND `tests/ui/main.cpp` (#include + qExec block).

---

## File Structure

**Modified:**
- `ui/include/gitgui/ui/AsyncRepo.hpp` — add `log(unsigned limit)` declaration.
- `ui/src/AsyncRepo.cpp` — implement `AsyncRepo::log`.
- `ui/include/gitgui/ui/Metatypes.hpp` — add `Q_DECLARE_METATYPE(gitgui::GraphLayout)`.
- `ui/include/gitgui/ui/RepoController.hpp` — add `refreshHistory` slot + `historyReady` signal.
- `ui/src/RepoController.cpp` — implement `refreshHistory`.
- `ui/include/gitgui/ui/MainWindow.hpp` — add `historyView_` member.
- `ui/src/MainWindow.cpp` — replace placeholder, connect repoOpened → refreshHistory.
- `ui/CMakeLists.txt` — add new files.
- `tests/CMakeLists.txt` — add new UI test file.
- `tests/ui/main.cpp` — add `#include` + `qExec` block.

**Created:**
- `ui/include/gitgui/ui/HistoryModel.hpp`
- `ui/src/HistoryModel.cpp`
- `ui/include/gitgui/ui/GraphDelegate.hpp`
- `ui/src/GraphDelegate.cpp`
- `ui/include/gitgui/ui/HistoryView.hpp`
- `ui/src/HistoryView.cpp`
- `tests/ui/test_history_model.cpp`

---

## Task 1: `AsyncRepo::log`

**Files:**
- Modify: `ui/include/gitgui/ui/AsyncRepo.hpp`
- Modify: `ui/src/AsyncRepo.cpp`
- Modify: `ui/include/gitgui/ui/Metatypes.hpp`

**Interfaces:**
- Consumes: `GitRepo::log(unsigned)` from Plan 5a; existing `AsyncRepo::Impl` (shared_ptr, mutex, GitRepo).
- Produces:
  ```cpp
  // In AsyncRepo:
  QCoro::Task<gitgui::Expected<std::vector<gitgui::CommitNode>>> log(unsigned limit = 1000);
  ```

- [ ] **Step 1: Add `log` to `AsyncRepo.hpp`**

Add `#include "gitgui/Graph.hpp"` after the existing includes. Add the declaration after `commit`:

```cpp
    QCoro::Task<gitgui::Expected<std::vector<gitgui::CommitNode>>>
    log(unsigned limit = 1000);
```

- [ ] **Step 2: Implement `AsyncRepo::log` in `AsyncRepo.cpp`**

Add after the existing `commit` implementation (same pattern — capture `impl_` by value):

```cpp
QCoro::Task<gitgui::Expected<std::vector<gitgui::CommitNode>>>
AsyncRepo::log(unsigned limit) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, limit] {
        std::scoped_lock lk(impl->mu);
        return impl->repo.log(limit);
    });
}
```

- [ ] **Step 3: Add `GraphLayout` metatype**

In `ui/include/gitgui/ui/Metatypes.hpp`, add:

```cpp
#include "gitgui/Graph.hpp"
// ... after existing Q_DECLARE_METATYPE lines:
Q_DECLARE_METATYPE(gitgui::GraphLayout)
```

- [ ] **Step 4: Build — verify it compiles**

```bash
cmake --build /home/michal/Documents/gitgui/build -j 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gitgui/ui/AsyncRepo.hpp ui/src/AsyncRepo.cpp \
        ui/include/gitgui/ui/Metatypes.hpp
git commit -m "feat(ui): AsyncRepo::log + Q_DECLARE_METATYPE GraphLayout"
```

---

## Task 2: `HistoryModel`

**Files:**
- Create: `ui/include/gitgui/ui/HistoryModel.hpp`
- Create: `ui/src/HistoryModel.cpp`
- Modify: `ui/CMakeLists.txt`
- Create: `tests/ui/test_history_model.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `GraphLayout`, `GraphRow`, `CommitNode` from `Graph.hpp`.
- Produces:
  ```cpp
  namespace gitgui::ui {
  class HistoryModel : public QAbstractTableModel {
  public:
      enum Column { ColGraph = 0, ColSummary, ColAuthor, ColDate, ColCount };
      enum Role   { GraphRowRole = Qt::UserRole + 1 };
      explicit HistoryModel(QObject* parent = nullptr);
      void setLayout(const gitgui::GraphLayout& layout);
      int laneCount() const;
      // QAbstractTableModel overrides: rowCount, columnCount, data, headerData
  };
  }
  ```

- [ ] **Step 1: Write failing tests**

Create `tests/ui/test_history_model.cpp`:

```cpp
#pragma once
#include <QObject>
#include <QtTest/QtTest>
#include "gitgui/ui/HistoryModel.hpp"
#include "gitgui/Graph.hpp"

namespace {
gitgui::GraphLayout make_linear_layout() {
    gitgui::CommitNode c1; c1.oid = "aaa"; c1.summary = "First";
                           c1.author = "Alice"; c1.time = 1000; c1.lane = 0;
    gitgui::CommitNode c2; c2.oid = "bbb"; c2.summary = "Second";
                           c2.author = "Bob";   c2.time = 2000; c2.lane = 0;
    gitgui::GraphRow r1; r1.commit = c2; r1.lineFromAbove = false;
                         r1.outEdges = {{0, 0}};
    gitgui::GraphRow r2; r2.commit = c1; r2.lineFromAbove = true;
    gitgui::GraphLayout layout;
    layout.rows = {r1, r2};
    layout.laneCount = 1;
    return layout;
}
}  // namespace

class TestHistoryModel : public QObject {
    Q_OBJECT
private slots:
    void empty_model_has_zero_rows() {
        gitgui::ui::HistoryModel m;
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.columnCount(), static_cast<int>(gitgui::ui::HistoryModel::ColCount));
    }

    void setLayout_updates_row_count() {
        gitgui::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.laneCount(), 1);
    }

    void display_role_summary_column() {
        gitgui::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gitgui::ui::HistoryModel::ColSummary);
        QCOMPARE(m.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Second"));
    }

    void display_role_author_column() {
        gitgui::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gitgui::ui::HistoryModel::ColAuthor);
        QCOMPARE(m.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Bob"));
    }

    void display_role_date_column_non_empty() {
        gitgui::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gitgui::ui::HistoryModel::ColDate);
        QVERIFY(!m.data(idx, Qt::DisplayRole).toString().isEmpty());
    }

    void graph_row_role_returns_GraphRow() {
        gitgui::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gitgui::ui::HistoryModel::ColGraph);
        auto val = m.data(idx, gitgui::ui::HistoryModel::GraphRowRole);
        QVERIFY(val.canConvert<gitgui::GraphRow>());
        auto row = val.value<gitgui::GraphRow>();
        QCOMPARE(row.commit.lane, 0);
        QCOMPARE(QString::fromStdString(row.commit.summary), QStringLiteral("Second"));
    }

    void header_data_returns_column_names() {
        gitgui::ui::HistoryModel m;
        QCOMPARE(m.headerData(gitgui::ui::HistoryModel::ColSummary,
                               Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Summary"));
    }
};
```

- [ ] **Step 2: Add test to CMakeLists and main**

In `tests/CMakeLists.txt`, add to `gitgui_ui_test_sources`:
```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_history_model.cpp
```

In `tests/ui/main.cpp`, add:
```cpp
#include "test_history_model.cpp"
```
And in `main()`:
```cpp
    {
        TestHistoryModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
```

- [ ] **Step 3: Run — verify FAIL to compile**

```bash
cmake --build /home/michal/Documents/gitgui/build -j 2>&1 | tail -10
```

Expected: compile error — `HistoryModel` not declared.

- [ ] **Step 4: Create `HistoryModel.hpp`**

```cpp
// ui/include/gitgui/ui/HistoryModel.hpp
#pragma once
#include <QAbstractTableModel>
#include "gitgui/Graph.hpp"
#include "gitgui/ui/Metatypes.hpp"

namespace gitgui::ui {

class HistoryModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColGraph = 0, ColSummary, ColAuthor, ColDate, ColCount };
    enum Role   { GraphRowRole = Qt::UserRole + 1 };

    explicit HistoryModel(QObject* parent = nullptr);

    void setLayout(const gitgui::GraphLayout& layout);
    int laneCount() const { return layout_.laneCount; }

    int     rowCount   (const QModelIndex& parent = {}) const override;
    int     columnCount(const QModelIndex& parent = {}) const override;
    QVariant data      (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    gitgui::GraphLayout layout_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 5: Create `HistoryModel.cpp`**

```cpp
// ui/src/HistoryModel.cpp
#include "gitgui/ui/HistoryModel.hpp"
#include <QDateTime>

namespace gitgui::ui {

HistoryModel::HistoryModel(QObject* parent) : QAbstractTableModel(parent) {
    qRegisterMetaType<gitgui::GraphLayout>();
    qRegisterMetaType<gitgui::GraphRow>();
}

void HistoryModel::setLayout(const gitgui::GraphLayout& layout) {
    beginResetModel();
    layout_ = layout;
    endResetModel();
}

int HistoryModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(layout_.rows.size());
}

int HistoryModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= layout_.rows.size()) return {};
    const auto& gr = layout_.rows[row];

    if (role == GraphRowRole && index.column() == ColGraph)
        return QVariant::fromValue(gr);

    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
        case ColGraph:   return {};
        case ColSummary: return QString::fromStdString(gr.commit.summary);
        case ColAuthor:  return QString::fromStdString(gr.commit.author);
        case ColDate: {
            QDateTime dt = QDateTime::fromSecsSinceEpoch(gr.commit.time);
            return dt.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        }
        default: return {};
    }
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case ColGraph:   return QStringLiteral("Graph");
        case ColSummary: return QStringLiteral("Summary");
        case ColAuthor:  return QStringLiteral("Author");
        case ColDate:    return QStringLiteral("Date");
        default:         return {};
    }
}

}  // namespace gitgui::ui
```

- [ ] **Step 6: Add `GraphRow` metatype to `Metatypes.hpp`**

`GraphRowRole` returns `QVariant::fromValue<gitgui::GraphRow>`, so `GraphRow` needs its own declaration. Add to `Metatypes.hpp`:

```cpp
Q_DECLARE_METATYPE(gitgui::GraphRow)
```

- [ ] **Step 7: Add files to `ui/CMakeLists.txt`**

In `ui/CMakeLists.txt`, add to `add_library(gitgui_ui ...)`:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HistoryModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/HistoryModel.hpp
```

- [ ] **Step 8: Build + run — verify PASS**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

Expected: all 6 new HistoryModel tests pass, no regressions.

- [ ] **Step 9: Commit**

```bash
git add ui/include/gitgui/ui/HistoryModel.hpp ui/src/HistoryModel.cpp \
        ui/include/gitgui/ui/Metatypes.hpp \
        ui/CMakeLists.txt \
        tests/CMakeLists.txt tests/ui/test_history_model.cpp tests/ui/main.cpp
git commit -m "feat(ui): HistoryModel — QAbstractTableModel backed by GraphLayout"
```

---

## Task 3: `GraphDelegate` + `HistoryView`

**Files:**
- Create: `ui/include/gitgui/ui/GraphDelegate.hpp`
- Create: `ui/src/GraphDelegate.cpp`
- Create: `ui/include/gitgui/ui/HistoryView.hpp`
- Create: `ui/src/HistoryView.cpp`
- Modify: `ui/CMakeLists.txt`

**Interfaces:**
- Consumes: `HistoryModel`, `GraphRowRole`, `GraphRow` from Task 2.
- Produces:
  ```cpp
  // GraphDelegate: custom painter for ColGraph column
  class GraphDelegate : public QStyledItemDelegate {
  public:
      explicit GraphDelegate(QObject* parent = nullptr);
      void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
      QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
      void setLaneCount(int count);  // called by HistoryView when model resets
  };

  // HistoryView: widget wrapping QTableView + HistoryModel + GraphDelegate
  class HistoryView : public QWidget {
  public:
      explicit HistoryView(QWidget* parent = nullptr);
      void setLayout(const gitgui::GraphLayout& layout);  // NOTE: shadows QWidget::setLayout
  };
  ```

No unit tests for delegate painting (visual output). `HistoryView` is exercised in Task 5 (MainWindow integration).

- [ ] **Step 1: Create `GraphDelegate.hpp`**

```cpp
// ui/include/gitgui/ui/GraphDelegate.hpp
#pragma once
#include <QStyledItemDelegate>

namespace gitgui::ui {

class GraphDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    static constexpr int kLaneWidth = 16;   // pixels per lane column
    static constexpr int kDotRadius = 4;    // circle radius in pixels

    explicit GraphDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void setLaneCount(int count) { laneCount_ = std::max(1, count); }

private:
    int laneCount_ = 1;

    static int laneX(int lane) { return lane * kLaneWidth + kLaneWidth / 2; }
};

}  // namespace gitgui::ui
```

- [ ] **Step 2: Create `GraphDelegate.cpp`**

```cpp
// ui/src/GraphDelegate.cpp
#include "gitgui/ui/GraphDelegate.hpp"
#include "gitgui/ui/HistoryModel.hpp"
#include "gitgui/Graph.hpp"

#include <QPainter>
#include <QStyleOptionViewItem>
#include <algorithm>

namespace gitgui::ui {

GraphDelegate::GraphDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize GraphDelegate::sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& /*index*/) const {
    return QSize(laneCount_ * kLaneWidth + kLaneWidth, option.rect.height());
}

void GraphDelegate::paint(QPainter* painter,
                           const QStyleOptionViewItem& option,
                           const QModelIndex& index) const {
    // Only handle the graph column; delegate to base for all others.
    if (index.column() != HistoryModel::ColGraph) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    auto val = index.data(HistoryModel::GraphRowRole);
    if (!val.canConvert<gitgui::GraphRow>()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const auto row = val.value<gitgui::GraphRow>();

    painter->save();
    painter->setClipRect(option.rect);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QColor lineColor = painter->pen().color();
    QPen linePen(lineColor, 1.5);
    painter->setPen(linePen);

    const int top = option.rect.top();
    const int bot = option.rect.bottom();
    const int mid = option.rect.center().y();
    const int cx  = option.rect.left() + laneX(row.commit.lane);

    auto x = [&](int lane) { return option.rect.left() + laneX(lane); };

    // 1. Pass-through lines (full cell height).
    for (int lane : row.passThroughs)
        painter->drawLine(x(lane), top, x(lane), bot);

    // 2. Incoming line to circle (top half of cell).
    if (row.lineFromAbove)
        painter->drawLine(cx, top, cx, mid);

    // 3. Commit circle.
    painter->setBrush(lineColor);
    painter->drawEllipse(QPoint(cx, mid), kDotRadius, kDotRadius);
    painter->setBrush(Qt::NoBrush);

    // 4. Outgoing edges (bottom half of cell) — from circle to parent lanes.
    for (const auto& e : row.outEdges)
        painter->drawLine(x(e.fromLane), mid, x(e.toLane), bot);

    painter->restore();
}

}  // namespace gitgui::ui
```

- [ ] **Step 3: Create `HistoryView.hpp`**

```cpp
// ui/include/gitgui/ui/HistoryView.hpp
#pragma once
#include <QWidget>
#include "gitgui/Graph.hpp"

class QTableView;

namespace gitgui::ui {

class HistoryModel;
class GraphDelegate;

class HistoryView : public QWidget {
    Q_OBJECT
public:
    explicit HistoryView(QWidget* parent = nullptr);

    // Loads a new GraphLayout into the model and resets the view.
    // Named setHistory to avoid shadowing QWidget::setLayout.
    void setHistory(const gitgui::GraphLayout& layout);

private:
    HistoryModel*   model_;
    GraphDelegate*  delegate_;
    QTableView*     view_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 4: Create `HistoryView.cpp`**

```cpp
// ui/src/HistoryView.cpp
#include "gitgui/ui/HistoryView.hpp"
#include "gitgui/ui/HistoryModel.hpp"
#include "gitgui/ui/GraphDelegate.hpp"

#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>

namespace gitgui::ui {

HistoryView::HistoryView(QWidget* parent)
    : QWidget(parent),
      model_   (new HistoryModel(this)),
      delegate_(new GraphDelegate(this)),
      view_    (new QTableView(this)) {

    view_->setModel(model_);
    view_->setItemDelegateForColumn(HistoryModel::ColGraph, delegate_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setShowGrid(false);
    view_->verticalHeader()->hide();
    view_->horizontalHeader()->setStretchLastSection(false);
    view_->horizontalHeader()->setSectionResizeMode(HistoryModel::ColSummary,
                                                    QHeaderView::Stretch);
    view_->setObjectName(QStringLiteral("historyTable"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);
}

void HistoryView::setHistory(const gitgui::GraphLayout& layout) {
    model_->setLayout(layout);
    delegate_->setLaneCount(layout.laneCount);
    // Resize graph column to fit lanes.
    view_->horizontalHeader()->resizeSection(
        HistoryModel::ColGraph,
        std::max(1, layout.laneCount) * GraphDelegate::kLaneWidth + GraphDelegate::kLaneWidth);
}

}  // namespace gitgui::ui
```

- [ ] **Step 5: Add new files to `ui/CMakeLists.txt`**

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/GraphDelegate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/GraphDelegate.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HistoryView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/HistoryView.hpp
```

- [ ] **Step 6: Build — verify it compiles**

```bash
cmake --build /home/michal/Documents/gitgui/build -j 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gitgui/ui/GraphDelegate.hpp ui/src/GraphDelegate.cpp \
        ui/include/gitgui/ui/HistoryView.hpp   ui/src/HistoryView.cpp \
        ui/CMakeLists.txt
git commit -m "feat(ui): GraphDelegate painter + HistoryView with QTableView"
```

---

## Task 4: `RepoController::refreshHistory`

**Files:**
- Modify: `ui/include/gitgui/ui/RepoController.hpp`
- Modify: `ui/src/RepoController.cpp`

**Interfaces:**
- Consumes: `AsyncRepo::log(unsigned)` from Task 1; `GraphBuilder::build` from Plan 5a.
- Produces:
  ```cpp
  // New in RepoController:
  signals:
      void historyReady(gitgui::GraphLayout layout);
  public slots:
      QCoro::Task<void> refreshHistory(unsigned limit = 1000);
  ```

- [ ] **Step 1: Update `RepoController.hpp`**

Add `#include "gitgui/Graph.hpp"` and `#include "gitgui/GraphBuilder.hpp"` after existing includes. Add to `signals:`:

```cpp
    void historyReady(gitgui::GraphLayout layout);
```

Add to `public slots:`:

```cpp
    QCoro::Task<void> refreshHistory(unsigned limit = 1000);
```

- [ ] **Step 2: Implement `refreshHistory` in `RepoController.cpp`**

Add `#include "gitgui/GraphBuilder.hpp"` at the top. Add after the `commit` implementation:

```cpp
QCoro::Task<void> RepoController::refreshHistory(unsigned limit) {
    if (!repo_) co_return;
    auto result = co_await repo_->log(limit);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit historyReady(gitgui::GraphBuilder::build(std::move(*result)));
}
```

- [ ] **Step 3: Build + run**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

Expected: all existing tests pass, no regressions.

- [ ] **Step 4: Commit**

```bash
git add ui/include/gitgui/ui/RepoController.hpp ui/src/RepoController.cpp
git commit -m "feat(ui): RepoController::refreshHistory async slot + historyReady signal"
```

---

## Task 5: MainWindow — History tab wiring

**Files:**
- Modify: `ui/include/gitgui/ui/MainWindow.hpp`
- Modify: `ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: `HistoryView::setHistory`, `RepoController::historyReady`, `RepoController::refreshHistory`, `RepoController::repoOpened` (existing signal).
- No new signals or public methods.

- [ ] **Step 1: Update `MainWindow.hpp`**

Add `#include "gitgui/ui/HistoryView.hpp"`. Add to the private member section:

```cpp
    HistoryView* historyView_;
```

- [ ] **Step 2: Update `MainWindow.cpp`**

Add `#include "gitgui/ui/HistoryView.hpp"` to the includes.

In the constructor member-initializer list, after `changesView_`:
```cpp
      historyView_(new HistoryView(this)),
```

Find the line that creates the tabs:
```cpp
    tabs->addTab(new QLabel(QStringLiteral("History — Plan 4")), QStringLiteral("History"));
```

Replace it with:
```cpp
    tabs->addTab(historyView_, QStringLiteral("History"));
```

After the `repoOpened` connection block (the one that calls `refreshStatus`), add:

```cpp
    connect(repoController_, &RepoController::repoOpened, this, [this](const QString&) {
        QCoro::connect(repoController_->refreshHistory(), this, [] {});
    });
    connect(repoController_, &RepoController::historyReady, this,
            [this](const gitgui::GraphLayout& layout) {
                historyView_->setHistory(layout);
            });
```

- [ ] **Step 3: Build + run all tests**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build --output-on-failure
```

Expected: all Core + UI tests pass. The History tab now shows `HistoryView` instead of the placeholder label.

- [ ] **Step 4: Commit**

```bash
git add ui/include/gitgui/ui/MainWindow.hpp ui/src/MainWindow.cpp
git commit -m "feat(ui): wire HistoryView into MainWindow History tab"
```

---

## Self-Review

**1. Spec coverage:**
- Per-repo commit graph (branches, merges) — ✅ Tasks 1–5 (graph data from Plan 5a, rendered here).
- Graph rendered in UI — ✅ Task 3 (`GraphDelegate` + `HistoryView` with `QTableView`).
- Lazy/virtualized rendering for 100k commits — ✅ `QTableView` + `QAbstractTableModel` only calls `paint()` for visible rows; `HistoryModel` holds the data in memory (~200 bytes/commit = 20 MB for 100k, acceptable).
- Async, never blocks UI thread — ✅ Task 4 (`refreshHistory` uses `QtConcurrent::run` via `AsyncRepo::log`, surfaced via `QCoro::connect`).
- History tab in MainWindow — ✅ Task 5.

**2. Placeholder scan:** None. All code shown inline.

**3. Type consistency:**
- `GraphRowRole` declared in `HistoryModel` (Task 2), queried in `GraphDelegate::paint` via `HistoryModel::GraphRowRole` — consistent.
- `GraphRow` metatype: `Q_DECLARE_METATYPE(gitgui::GraphRow)` added to `Metatypes.hpp` in Task 2; `qRegisterMetaType<gitgui::GraphRow>()` called in `HistoryModel` constructor — consistent.
- `HistoryView::setHistory` (not `setLayout`) avoids shadowing `QWidget::setLayout`; called in `MainWindow.cpp` as `historyView_->setHistory(layout)` — consistent.
- `AsyncRepo::log` returns `QCoro::Task<gitgui::Expected<std::vector<gitgui::CommitNode>>>` (Task 1), dereferenced with `*result` in `refreshHistory` (Task 4) after `has_value()` check — consistent.
- `GraphBuilder::build(std::move(*result))` takes `std::vector<CommitNode>` by value — matches Plan 5a's `static GraphLayout build(std::vector<CommitNode> commits)` — consistent.

**Notes:**
- `QTableView` + `QAbstractTableModel` virtualizes naturally — Qt only calls `data()` and `paint()` for visible rows. No custom scene management needed.
- Column ColGraph width is set explicitly in `HistoryView::setHistory` based on `laneCount`; ColSummary stretches to fill remaining space.
- `historyReady` signal passes `GraphLayout` by value (copy) — `GraphLayout` contains `std::vector<GraphRow>` which contains `std::vector<CommitNode>`. For typical repos (< 10k commits in the limit) this is fast; for very large layouts the copy could be replaced with `std::shared_ptr<const GraphLayout>` post-MVP.
- The two `repoOpened` connections in `MainWindow` are additive — the existing one triggers `refreshStatus`, the new one triggers `refreshHistory`. Both fire on every repo open.
