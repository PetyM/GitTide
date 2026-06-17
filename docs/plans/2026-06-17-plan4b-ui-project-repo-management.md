# Plan 4b — UI: Project / Repo Management
| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [product](../spec/product/product.md) |
| **Depends on** | [Plan 4a](2026-06-17-plan4a-core-init-clone-mutations.md) · [Plan 3b](2026-06-17-plan3b-async-changes-ui.md) |

**Goal:** Wire Plan 4a's Core mutations into the UI — project/repo creation dialogs, empty-state onboarding screens, sidebar toolbar, clone-with-progress modal.

**Architecture:** `ProjectController` gains mutation slots wired to `ProjectStore`; `RepoListModel` promoted to tree model; `ProjectSidebar` gains combo sentinel + toolbar; `MainWindow` gains `QStackedWidget` empty states; new lightweight dialogs for init and clone.

**Tech Stack:** C++23, Qt6, QCoro 0.11 (`QtConcurrent::run` + `co_await` pattern from `AsyncRepo`), libgit2 (via `GitRepo::init`/`GitRepo::clone`), Catch2/QtTest.

## Global Constraints

- All UI code lives in `ui/`; all Core (`core/`) is Qt-free.
- Mutation slots call `store_->createProject()` / `store_->addRepo()` then `store_->save(storePath_)` only if `storePath_` is non-empty (tests omit the path — no disk I/O).
- `Expected<T>` = `std::expected<T, GitError>` throughout.
- Coroutine slots take all args **by value** (survives `co_await` suspension) — same rule as `RepoController`.
- Fire-and-forget `QCoro::Task` always anchored via `QCoro::connect(task, this, []{})`.
- Qt object names (used in tests): `projectSwitcher`, `repoList`, `addExistingButton`, `initRepoButton`, `cloneButton`, `centralStack`, `createProjectCta`, `addExistingCta`, `initRepoCta`, `cloneCta`.
- `std::atomic<bool>` for the clone cancel flag (cross-thread, no mutex needed).
- No new Qt Quick / QML — pure Widgets.

---

## Task 1: ProjectController mutation slots

**Files:**
- Modify: `ui/include/gitgui/ui/ProjectController.hpp`
- Modify: `ui/src/ProjectController.cpp`
- Modify: `ui/include/gitgui/ui/MainWindow.hpp` (add `storePath` param)
- Modify: `ui/src/MainWindow.cpp` (pass `storePath` to `ProjectController`)
- Modify: `ui/src/WindowManager.cpp` (pass projects-file path when creating windows)
- Modify: `tests/ui/test_project_controller.cpp` (4 new test cases)

**Interfaces:**
- Consumes: `ProjectStore::createProject(name) -> Project&`, `ProjectStore::addRepo(id, RepoRef) -> Expected<void>`, `ProjectStore::save(path)`, `GitRepo::open(path)`, `GitRepo::init(path)`, `ProjectListModel::refresh()`
- Produces:
  ```cpp
  // In ProjectController:
  explicit ProjectController(gitgui::ProjectStore* store,
                             std::filesystem::path storePath = {},
                             QObject* parent = nullptr);
  public slots:
    void createProject(const QString& name);          // emits projectCreated(id), activates it
    void addExistingRepo(const QString& path);        // emits repoAdded or repoAddFailed
    void initRepo(const QString& parentDir,
                  const QString& name);              // emits repoAdded or repoAddFailed
  signals:
    void projectCreated(const QString& projectId);
    void repoAdded(const QString& path);
    void repoAddFailed(const QString& message);

  // In MainWindow:
  explicit MainWindow(gitgui::ProjectStore* store,
                      std::filesystem::path storePath = {},
                      QWidget* parent = nullptr);
  ```

- [ ] **Step 1: Update `ProjectController.hpp`**

Add `#include <filesystem>` and `std::filesystem::path storePath_` private member. Update constructor to accept `storePath` (default `{}`). Add signals and slots:

```cpp
// ProjectController.hpp
#pragma once
#include <QObject>
#include <QString>
#include <filesystem>
#include <vector>
#include "gitgui/ProjectStore.hpp"

namespace gitgui::ui {

class ProjectListModel;
class RepoListModel;

class ProjectController : public QObject {
    Q_OBJECT
public:
    explicit ProjectController(gitgui::ProjectStore* store,
                               std::filesystem::path storePath = {},
                               QObject* parent = nullptr);

    ProjectListModel* projects() const { return projectModel_; }
    RepoListModel* repos() const { return repoModel_; }
    QString activeProjectId() const { return activeId_; }
    const std::vector<gitgui::RepoRef>& activeRepos() const;

public slots:
    void activate(const QString& projectId);
    void createProject(const QString& name);
    void addExistingRepo(const QString& path);
    void initRepo(const QString& parentDir, const QString& name);

signals:
    void projectActivated(const QString& projectId);
    void projectCreated(const QString& projectId);
    void repoAdded(const QString& path);
    void repoAddFailed(const QString& message);

private:
    gitgui::ProjectStore* store_;
    std::filesystem::path storePath_;
    ProjectListModel* projectModel_;
    RepoListModel* repoModel_;
    QString activeId_;

    void saveStore() const;
    void refreshRepoModel();
};

}  // namespace gitgui::ui
```

- [ ] **Step 2: Implement new slots in `ProjectController.cpp`**

Add `#include "gitgui/GitRepo.hpp"` at top. Implement `saveStore`, `refreshRepoModel`, and the three new slots:

```cpp
// Add to existing includes:
#include "gitgui/GitRepo.hpp"
#include <filesystem>

void ProjectController::saveStore() const {
    if (!storePath_.empty()) store_->save(storePath_);
}

void ProjectController::refreshRepoModel() {
    for (const auto& p : store_->projects()) {
        if (QString::fromStdString(p.id) == activeId_) {
            repoModel_->setRepos(p.repos);
            return;
        }
    }
    repoModel_->setRepos({});
}

void ProjectController::createProject(const QString& name) {
    if (name.trimmed().isEmpty()) return;
    auto& p = store_->createProject(name.toStdString());
    saveStore();
    const QString id = QString::fromStdString(p.id);
    projectModel_->refresh();
    activate(id);
    emit projectCreated(id);
}

void ProjectController::addExistingRepo(const QString& path) {
    const std::filesystem::path p(path.toStdString());
    auto validation = gitgui::GitRepo::open(p);
    if (!validation) {
        emit repoAddFailed(QString::fromStdString(validation.error().message));
        return;
    }
    auto result = store_->addRepo(activeId_.toStdString(),
                                  gitgui::RepoRef{.path = path.toStdString()});
    if (!result) {
        emit repoAddFailed(QString::fromStdString(result.error().message));
        return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(path);
}

void ProjectController::initRepo(const QString& parentDir, const QString& name) {
    const std::filesystem::path dest =
        std::filesystem::path(parentDir.toStdString()) / name.toStdString();
    auto repo = gitgui::GitRepo::init(dest);
    if (!repo) {
        emit repoAddFailed(QString::fromStdString(repo.error().message));
        return;
    }
    auto result = store_->addRepo(activeId_.toStdString(),
                                  gitgui::RepoRef{.path = dest.generic_string()});
    if (!result) {
        emit repoAddFailed(QString::fromStdString(result.error().message));
        return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(QString::fromStdString(dest.generic_string()));
}
```

Also update the constructor to store `storePath`:
```cpp
ProjectController::ProjectController(gitgui::ProjectStore* store,
                                     std::filesystem::path storePath,
                                     QObject* parent)
    : QObject(parent),
      store_(store),
      storePath_(std::move(storePath)),
      projectModel_(new ProjectListModel(store, this)),
      repoModel_(new RepoListModel(this)) {}
```

- [ ] **Step 3: Update `MainWindow.hpp` constructor**

```cpp
explicit MainWindow(gitgui::ProjectStore* store,
                    std::filesystem::path storePath = {},
                    QWidget* parent = nullptr);
```

Add `#include <filesystem>` if not present. Add `std::filesystem::path storePath_` private member.

- [ ] **Step 4: Update `MainWindow.cpp` to pass storePath to ProjectController**

Change:
```cpp
controller_(new ProjectController(store, this)),
```
To:
```cpp
controller_(new ProjectController(store, std::move(storePath), this)),
```

In constructor body add `storePath_(std::move(storePath))` — but since we pass it along, actually just pass it to the controller and don't store it separately unless MainWindow needs it. Change constructor params:

```cpp
MainWindow::MainWindow(gitgui::ProjectStore* store,
                       std::filesystem::path storePath,
                       QWidget* parent)
    : QMainWindow(parent),
      controller_(new ProjectController(store, std::move(storePath), this)),
      ...
```

- [ ] **Step 5: Update `WindowManager.cpp` to pass projects file path**

In `WindowManager::createWindow()`, change:
```cpp
MainWindow* w = new MainWindow(&store_);
```
To:
```cpp
const std::filesystem::path projectsFile =
    std::filesystem::path(configDir_.toStdString()) / "projects.json";
MainWindow* w = new MainWindow(&store_, projectsFile);
```

- [ ] **Step 6: Write failing tests**

Append to `tests/ui/test_project_controller.cpp`. Tests need `#include <git2.h>` and `#include <filesystem>`. Add `initTestCase`/`cleanupTestCase` for libgit2:

```cpp
#include <git2.h>
#include <filesystem>
#include <fstream>
// ... existing includes ...

class TestProjectController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()    { git_libgit2_init(); }
    void cleanupTestCase() { git_libgit2_shutdown(); }

    // ... existing tests ...

    void createProject_appends_project_and_emits() {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spyCreated(&controller, &ProjectController::projectCreated);
        QSignalSpy spyActivated(&controller, &ProjectController::projectActivated);

        controller.createProject(QStringLiteral("Sandbox"));

        QCOMPARE(store.projects().size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(store.projects()[0].name), QStringLiteral("Sandbox"));
        QCOMPARE(spyCreated.count(), 1);
        QCOMPARE(spyActivated.count(), 1);
        QCOMPARE(controller.activeProjectId(), spyCreated.at(0).at(0).toString());
    }

    void createProject_empty_name_is_ignored() {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectCreated);

        controller.createProject(QStringLiteral("   "));

        QCOMPARE(store.projects().size(), std::size_t(0));
        QCOMPARE(spy.count(), 0);
    }

    void addExistingRepo_valid_repo_emits_repoAdded() {
        // Create a real git repo in /tmp
        auto dir = std::filesystem::temp_directory_path() /
                   ("gitgui-pc-add-" + std::to_string(rand()));
        std::filesystem::create_directories(dir);
        git_repository* raw = nullptr;
        git_repository_init(&raw, dir.generic_string().c_str(), 0);
        git_repository_free(raw);

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        controller.addExistingRepo(QString::fromStdString(dir.generic_string()));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));
        std::filesystem::remove_all(dir);
    }

    void addExistingRepo_nonrepo_emits_repoAddFailed() {
        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAddFailed);
        controller.addExistingRepo(QStringLiteral("/no/such/gitgui-notarepo"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(!spy.at(0).at(0).toString().isEmpty());
    }

    void initRepo_creates_repo_and_emits_repoAdded() {
        const auto parentDir = std::filesystem::temp_directory_path();
        const std::string repoName =
            "gitgui-pc-init-" + std::to_string(rand());
        const auto dest = parentDir / repoName;

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        controller.initRepo(
            QString::fromStdString(parentDir.generic_string()),
            QString::fromStdString(repoName));

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::filesystem::exists(dest / ".git"));
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));
        std::filesystem::remove_all(dest);
    }
};
```

- [ ] **Step 7: Build**

```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

Expected: compiles. Linker error on missing symbols = impl incomplete.

- [ ] **Step 8: Run UI tests**

```bash
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

Expected: all existing + 5 new tests pass.

- [ ] **Step 9: Commit**

```bash
git add ui/include/gitgui/ui/ProjectController.hpp \
        ui/src/ProjectController.cpp \
        ui/include/gitgui/ui/MainWindow.hpp \
        ui/src/MainWindow.cpp \
        ui/src/WindowManager.cpp \
        tests/ui/test_project_controller.cpp
git commit -m "feat(ui): ProjectController mutation slots — createProject / addExistingRepo / initRepo"
```

---

## Task 2: RepoListModel → QAbstractItemModel tree model

**Files:**
- Modify: `ui/include/gitgui/ui/RepoListModel.hpp`
- Modify: `ui/src/RepoListModel.cpp`
- Modify: `tests/ui/test_repo_list_model.cpp` (2 new tree-model tests)

**Interfaces:**
- Produces: same public interface as before (`setRepos()`, `PathRole`, `MissingRole`, `Qt::DisplayRole`) but base class is now `QAbstractItemModel`. `QTreeView::setModel()` and `QListView::setModel()` both accept `QAbstractItemModel*`, so all consumers continue to compile.

The key tree-model contract for a single-level (flat) model:
- `index(row, col, parent)` → invalid if `parent.isValid()` or out-of-range; else `createIndex(row, col, nullptr)`
- `parent(index)` → always `QModelIndex{}` (top-level items have no parent)
- `rowCount(parent)` → 0 if `parent.isValid()`; else `rows_.size()`
- `columnCount(parent)` → 0 if `parent.isValid()`; else `1`
- `data(index, role)` → same logic as before

- [ ] **Step 1: Write failing tests**

Append to `tests/ui/test_repo_list_model.cpp`:

```cpp
    void tree_model_parent_of_top_level_item_is_invalid() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api", .alias = "api"} };
        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);

        QVERIFY(!model.parent(model.index(0, 0)).isValid());
    }

    void tree_model_top_level_items_have_no_children() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api", .alias = "api"} };
        RepoListModel model;
        model.setRepos(repos);

        const QModelIndex item = model.index(0, 0);
        QCOMPARE(model.rowCount(item), 0);
    }

    void tree_model_index_with_valid_parent_is_invalid() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api", .alias = "api"} };
        RepoListModel model;
        model.setRepos(repos);

        const QModelIndex parent = model.index(0, 0);
        QVERIFY(!model.index(0, 0, parent).isValid());
    }
```

Run tests → expect compile error (model not yet a tree model):
```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

- [ ] **Step 2: Update `RepoListModel.hpp`**

Change base class and add missing overrides:

```cpp
#pragma once
#include <QAbstractItemModel>
#include <vector>
#include "gitgui/ProjectStore.hpp"

namespace gitgui::ui {

class RepoListModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1, MissingRole };

    explicit RepoListModel(QObject* parent = nullptr);

    // QAbstractItemModel overrides
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRepos(const std::vector<gitgui::RepoRef>& repos);

private:
    struct Row { QString alias; QString path; bool missing; };
    std::vector<Row> rows_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 3: Implement tree overrides in `RepoListModel.cpp`**

Replace `#include <QAbstractListModel>` with `#include <QAbstractItemModel>`. Replace all method bodies:

```cpp
#include "gitgui/ui/RepoListModel.hpp"
#include <filesystem>

namespace gitgui::ui {

RepoListModel::RepoListModel(QObject* parent) : QAbstractItemModel(parent) {}

void RepoListModel::setRepos(const std::vector<gitgui::RepoRef>& repos) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(repos.size());
    for (const auto& r : repos) {
        const std::filesystem::path p = std::filesystem::path(r.path);
        std::error_code ec;
        const bool present = std::filesystem::exists(p, ec) && !ec;
        rows_.push_back(Row{
            .alias = QString::fromStdString(r.alias),
            .path  = QString::fromStdString(r.path),
            .missing = !present,
        });
    }
    endResetModel();
}

QModelIndex RepoListModel::index(int row, int column,
                                  const QModelIndex& parent) const {
    if (parent.isValid()) return {};
    if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
    if (column != 0) return {};
    return createIndex(row, column, nullptr);
}

QModelIndex RepoListModel::parent(const QModelIndex&) const {
    return {};
}

int RepoListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

int RepoListModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 1;
}

QVariant RepoListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= rows_.size()) return {};
    const auto& r = rows_[row];
    switch (role) {
        case Qt::DisplayRole: return r.alias.isEmpty() ? r.path : r.alias;
        case PathRole:        return r.path;
        case MissingRole:     return r.missing;
        default:              return {};
    }
}

QHash<int, QByteArray> RepoListModel::roleNames() const {
    auto roles = QAbstractItemModel::roleNames();
    roles[PathRole]   = "repoPath";
    roles[MissingRole] = "missing";
    return roles;
}

}  // namespace gitgui::ui
```

- [ ] **Step 4: Build and run tests**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

Expected: all UI tests pass (3 existing RepoListModel tests + 3 new tree tests). `QAbstractItemModelTester` validates the tree contract internally.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gitgui/ui/RepoListModel.hpp \
        ui/src/RepoListModel.cpp \
        tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): promote RepoListModel to QAbstractItemModel tree (single level)"
```

---

## Task 3: ProjectSidebar — combo sentinel, QTreeView, toolbar

**Files:**
- Modify: `ui/include/gitgui/ui/ProjectSidebar.hpp`
- Modify: `ui/src/ProjectSidebar.cpp`
- Modify: `tests/ui/test_project_sidebar.cpp` (update existing + 3 new tests)

**Interfaces:**
- Consumes: `ProjectController::projects()` (ProjectListModel), `ProjectListModel::IdRole`, `ProjectController::projectCreated(id)` signal, `ProjectController::projectActivated(id)` signal
- Produces new signals:
  ```cpp
  signals:
    void createProjectRequested();
    void addExistingRequested();
    void initRepoRequested();
    void cloneRepoRequested();
  ```
- `repoList_` changes from `QListView*` to `QTreeView*`. Existing `repoSelected` signal unchanged.
- Combo is manually synced (no `setModel()`). `syncCombo()` is a private slot. Sentinel item data = `"__new__"`.

- [ ] **Step 1: Write failing tests**

Replace the existing `QCOMPARE(combo->count(), 2)` with `QCOMPARE(combo->count(), 3)` (2 projects + sentinel). Add new tests at the end of `TestProjectSidebar`:

```cpp
    // In existing test: update count assertion
    // QCOMPARE(combo->count(), 2);  →  QCOMPARE(combo->count(), 3);

    void new_project_sentinel_is_last_combo_item() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        QVERIFY(combo != nullptr);
        // sentinel is last
        QCOMPARE(combo->itemText(combo->count() - 1), QStringLiteral("New project…"));
    }

    void selecting_sentinel_emits_createProjectRequested_not_activate() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        // First activate a real project
        combo->setCurrentIndex(0);
        const QString prevActive = controller.activeProjectId();

        QSignalSpy spyNew(&sidebar, &ProjectSidebar::createProjectRequested);
        QSignalSpy spyActivated(&controller, &ProjectController::projectActivated);
        // Select sentinel
        combo->setCurrentIndex(combo->count() - 1);

        QCOMPARE(spyNew.count(), 1);
        // Active project must not have changed
        QCOMPARE(controller.activeProjectId(), prevActive);
    }

    void toolbar_buttons_exist_with_correct_objectNames() {
        ProjectStore store;
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        QVERIFY(sidebar.findChild<QAbstractButton*>(QStringLiteral("addExistingButton")) != nullptr);
        QVERIFY(sidebar.findChild<QAbstractButton*>(QStringLiteral("initRepoButton")) != nullptr);
        QVERIFY(sidebar.findChild<QAbstractButton*>(QStringLiteral("cloneButton")) != nullptr);
    }

    void add_existing_button_emits_addExistingRequested() {
        ProjectStore store;
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        QSignalSpy spy(&sidebar, &ProjectSidebar::addExistingRequested);
        sidebar.findChild<QAbstractButton*>(QStringLiteral("addExistingButton"))->click();
        QCOMPARE(spy.count(), 1);
    }
```

Run tests — expect failures on new assertions:
```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

- [ ] **Step 2: Update `ProjectSidebar.hpp`**

```cpp
#pragma once
#include <QWidget>
#include <QString>

class QComboBox;
class QTreeView;
class QToolButton;

namespace gitgui::ui {

class ProjectController;

class ProjectSidebar : public QWidget {
    Q_OBJECT
public:
    explicit ProjectSidebar(ProjectController* controller, QWidget* parent = nullptr);

public slots:
    void requestOpenInNewWindow();

signals:
    void openInNewWindowRequested(const QString& projectId);
    void repoSelected(const QString& repoPath);
    void createProjectRequested();
    void addExistingRequested();
    void initRepoRequested();
    void cloneRepoRequested();

private slots:
    void syncCombo();

private:
    ProjectController* controller_;
    QComboBox* switcher_;
    QTreeView* repoList_;
    QToolButton* addExistingBtn_;
    QToolButton* initRepoBtn_;
    QToolButton* cloneBtn_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 3: Rewrite `ProjectSidebar.cpp`**

```cpp
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectListModel.hpp"
#include "gitgui/ui/RepoListModel.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace gitgui::ui {

static constexpr auto kSentinel = "__new__";

ProjectSidebar::ProjectSidebar(ProjectController* controller, QWidget* parent)
    : QWidget(parent),
      controller_(controller),
      switcher_(new QComboBox(this)),
      repoList_(new QTreeView(this)),
      addExistingBtn_(new QToolButton(this)),
      initRepoBtn_(new QToolButton(this)),
      cloneBtn_(new QToolButton(this)) {

    switcher_->setObjectName(QStringLiteral("projectSwitcher"));

    repoList_->setObjectName(QStringLiteral("repoList"));
    repoList_->setModel(controller_->repos());
    repoList_->header()->hide();
    repoList_->setRootIsDecorated(false);
    repoList_->setContextMenuPolicy(Qt::CustomContextMenu);

    addExistingBtn_->setObjectName(QStringLiteral("addExistingButton"));
    addExistingBtn_->setText(QStringLiteral("Add"));
    addExistingBtn_->setToolTip(QStringLiteral("Add existing repository"));

    initRepoBtn_->setObjectName(QStringLiteral("initRepoButton"));
    initRepoBtn_->setText(QStringLiteral("Init"));
    initRepoBtn_->setToolTip(QStringLiteral("Initialize new repository"));

    cloneBtn_->setObjectName(QStringLiteral("cloneButton"));
    cloneBtn_->setText(QStringLiteral("Clone"));
    cloneBtn_->setToolTip(QStringLiteral("Clone repository from URL"));

    auto* toolbar = new QWidget(this);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->addWidget(addExistingBtn_);
    toolbarLayout->addWidget(initRepoBtn_);
    toolbarLayout->addWidget(cloneBtn_);
    toolbarLayout->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(switcher_);
    layout->addWidget(repoList_, /*stretch=*/1);
    layout->addWidget(toolbar);

    // Populate combo and keep it in sync
    syncCombo();
    connect(controller_, &ProjectController::projectCreated, this,
            [this](const QString&) { syncCombo(); });
    connect(controller_, &ProjectController::projectActivated, this,
            [this](const QString&) { syncCombo(); });

    // Selecting a repo row emits repoSelected.
    connect(repoList_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (!current.isValid()) return;
                emit repoSelected(
                    current.data(RepoListModel::PathRole).toString());
            });

    // Combo selection — detect sentinel.
    connect(switcher_, &QComboBox::currentIndexChanged, this, [this](int row) {
        if (row < 0) return;
        const QString id = switcher_->itemData(row).toString();
        if (id == QString::fromLatin1(kSentinel)) {
            emit createProjectRequested();
            syncCombo();  // restores selection to current active project
            return;
        }
        controller_->activate(id);
    });

    // Toolbar buttons
    connect(addExistingBtn_, &QToolButton::clicked, this,
            &ProjectSidebar::addExistingRequested);
    connect(initRepoBtn_, &QToolButton::clicked, this,
            &ProjectSidebar::initRepoRequested);
    connect(cloneBtn_, &QToolButton::clicked, this,
            &ProjectSidebar::cloneRepoRequested);
}

void ProjectSidebar::syncCombo() {
    const QSignalBlocker blocker(switcher_);
    switcher_->clear();
    auto* model = controller_->projects();
    for (int i = 0; i < model->rowCount(); ++i) {
        switcher_->addItem(
            model->data(model->index(i), Qt::DisplayRole).toString(),
            model->data(model->index(i), ProjectListModel::IdRole));
    }
    switcher_->addItem(QStringLiteral("New project…"),
                       QString::fromLatin1(kSentinel));

    // Restore active project selection
    const QString activeId = controller_->activeProjectId();
    if (!activeId.isEmpty()) {
        for (int i = 0; i < model->rowCount(); ++i) {
            if (switcher_->itemData(i).toString() == activeId) {
                switcher_->setCurrentIndex(i);
                return;
            }
        }
    }
    switcher_->setCurrentIndex(-1);
}

void ProjectSidebar::requestOpenInNewWindow() {
    const QString id = controller_->activeProjectId();
    if (!id.isEmpty()) emit openInNewWindowRequested(id);
}

}  // namespace gitgui::ui
```

Note: "New project…" uses Unicode ellipsis `…` (single char) rather than three dots.

- [ ] **Step 4: Build and run tests**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

Expected: all UI tests pass. The updated `QCOMPARE(combo->count(), 3)` assertion now passes.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gitgui/ui/ProjectSidebar.hpp \
        ui/src/ProjectSidebar.cpp \
        tests/ui/test_project_sidebar.cpp
git commit -m "feat(ui): ProjectSidebar — combo sentinel, QTreeView, add-repo toolbar"
```

---

## Task 4: MainWindow empty states + dialog wiring (add existing, init, create project)

**Files:**
- Modify: `ui/include/gitgui/ui/MainWindow.hpp`
- Modify: `ui/src/MainWindow.cpp`
- Create: `ui/include/gitgui/ui/AddRepoDialogs.hpp`
- Create: `ui/src/AddRepoDialogs.cpp`
- Modify: `ui/CMakeLists.txt` (add new dialog files)
- Modify: `tests/ui/test_main_window.cpp` (update + 3 new tests)

**Interfaces:**
- Consumes: `ProjectSidebar::createProjectRequested`, `addExistingRequested`, `initRepoRequested`, `cloneRepoRequested` signals; `ProjectController::createProject`, `addExistingRepo`, `initRepo` slots; `ProjectController::projectCreated`, `repoAdded` signals
- Produces:
  ```cpp
  // InitRepoDialog (ui/include/gitgui/ui/AddRepoDialogs.hpp)
  class InitRepoDialog : public QDialog {
      Q_OBJECT
  public:
      explicit InitRepoDialog(QWidget* parent = nullptr);
      QString parentDir() const;
      QString repoName() const;
  };
  ```
- `MainWindow` central widget becomes a `QStackedWidget` (objectName `centralStack`) with:
  - Index 0: "no projects" page — centered `QPushButton` (objectName `createProjectCta`)
  - Index 1: "no repos" page — 3 `QPushButton`s (objectNames `addExistingCta`, `initRepoCta`, `cloneCta`)
  - Index 2: existing `QTabWidget` (objectName `mainTabs`)
- `updateCentralPage()` private method: index 0 if `store_->projects().empty()`; index 1 if active project has no repos; index 2 otherwise.
- Clone wiring is wired as a **no-op stub** in this task (dialog + async comes in Task 5).

- [ ] **Step 1: Write failing tests**

Append to `tests/ui/test_main_window.cpp`:

```cpp
    void no_projects_shows_create_project_cta() {
        ProjectStore store;
        MainWindow win(&store);

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 0);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("createProjectCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void empty_project_shows_add_repo_cta() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Empty"});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 1);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("addExistingCta")) != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("initRepoCta")) != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("cloneCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void project_with_repos_shows_tabs() {
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = "/home/u/api", .alias = "api"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 2);
        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        main_window_test::drainAsync();
    }
```

Add `#include <QStackedWidget>` to the test file's includes.

Run — expect compile error (QStackedWidget not yet in MainWindow):
```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

- [ ] **Step 2: Create `AddRepoDialogs.hpp`**

```cpp
// ui/include/gitgui/ui/AddRepoDialogs.hpp
#pragma once
#include <QDialog>

class QLineEdit;

namespace gitgui::ui {

// Dialog for "Initialize new repository" — parent directory + repo name.
class InitRepoDialog : public QDialog {
    Q_OBJECT
public:
    explicit InitRepoDialog(QWidget* parent = nullptr);
    QString parentDir() const;
    QString repoName() const;

private:
    QLineEdit* parentDirEdit_;
    QLineEdit* nameEdit_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 3: Create `AddRepoDialogs.cpp`**

```cpp
// ui/src/AddRepoDialogs.cpp
#include "gitgui/ui/AddRepoDialogs.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace gitgui::ui {

InitRepoDialog::InitRepoDialog(QWidget* parent)
    : QDialog(parent),
      parentDirEdit_(new QLineEdit(this)),
      nameEdit_(new QLineEdit(this)) {
    setWindowTitle(QStringLiteral("Initialize Repository"));

    auto* browse = new QPushButton(QStringLiteral("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Select parent directory"));
        if (!dir.isEmpty()) parentDirEdit_->setText(dir);
    });

    auto* form = new QFormLayout;
    auto* dirRow = new QWidget(this);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->addWidget(parentDirEdit_);
    dirLayout->addWidget(browse);

    form->addRow(QStringLiteral("Parent directory:"), dirRow);
    form->addRow(QStringLiteral("Repository name:"), nameEdit_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString InitRepoDialog::parentDir() const { return parentDirEdit_->text(); }
QString InitRepoDialog::repoName() const  { return nameEdit_->text(); }

}  // namespace gitgui::ui
```

- [ ] **Step 4: Update `ui/CMakeLists.txt`**

Add the new dialog files to `add_library(gitgui_ui ...)`:
```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AddRepoDialogs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/AddRepoDialogs.hpp
```

- [ ] **Step 5: Update `MainWindow.hpp`**

Add `#include <QStackedWidget>` forward declaration + private helpers. Add `store_` raw pointer (needed for `updateCentralPage`):

```cpp
#pragma once
#include <QMainWindow>
#include <QString>
#include <filesystem>

namespace gitgui { class ProjectStore; }

class QStackedWidget;

namespace gitgui::ui {

class ProjectController;
class ProjectSidebar;
class RepoController;
class ChangesView;
class DashboardModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(gitgui::ProjectStore* store,
                        std::filesystem::path storePath = {},
                        QWidget* parent = nullptr);

    ProjectController* controller() const { return controller_; }
    QString currentProjectId() const;
    void showProject(const QString& projectId);

signals:
    void openInNewWindowRequested(const QString& projectId);
    void repoOpened(const QString& path);

private slots:
    void updateCentralPage();
    void onCreateProjectRequested();
    void onAddExistingRequested();
    void onInitRepoRequested();
    void onCloneRepoRequested();  // stub in Task 4; implemented in Task 5

private:
    gitgui::ProjectStore* store_;
    ProjectController* controller_;
    ProjectSidebar* sidebar_;
    RepoController* repoController_;
    ChangesView* changesView_;
    DashboardModel* dashboardModel_;
    QStackedWidget* centralStack_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 6: Rewrite `MainWindow.cpp`**

Full replacement (retain all existing connections, add new ones):

```cpp
#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/DashboardModel.hpp"
#include "gitgui/ui/AddRepoDialogs.hpp"

#include <filesystem>
#include <vector>

#include <qcorotask.h>

#include <QDockWidget>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFileDialog>

namespace gitgui::ui {

// ---- helpers ----
namespace {
QWidget* makeNoProjectsPage(QWidget* parent) {
    auto* w = new QWidget(parent);
    w->setObjectName(QStringLiteral("noProjectsPage"));
    auto* btn = new QPushButton(QStringLiteral("Create Project"), w);
    btn->setObjectName(QStringLiteral("createProjectCta"));
    auto* layout = new QVBoxLayout(w);
    layout->addStretch();
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(btn);
    row->addStretch();
    layout->addLayout(row);
    layout->addStretch();
    return w;
}

QWidget* makeNoReposPage(QWidget* parent) {
    auto* w = new QWidget(parent);
    w->setObjectName(QStringLiteral("noReposPage"));
    auto* addBtn  = new QPushButton(QStringLiteral("Add Existing Repository"), w);
    addBtn->setObjectName(QStringLiteral("addExistingCta"));
    auto* initBtn = new QPushButton(QStringLiteral("Initialize New Repository"), w);
    initBtn->setObjectName(QStringLiteral("initRepoCta"));
    auto* cloneBtn = new QPushButton(QStringLiteral("Clone Repository"), w);
    cloneBtn->setObjectName(QStringLiteral("cloneCta"));
    auto* layout = new QVBoxLayout(w);
    layout->addStretch();
    for (auto* b : {addBtn, initBtn, cloneBtn}) {
        auto* row = new QHBoxLayout;
        row->addStretch(); row->addWidget(b); row->addStretch();
        layout->addLayout(row);
    }
    layout->addStretch();
    return w;
}
}  // namespace

// ---- MainWindow ----
MainWindow::MainWindow(gitgui::ProjectStore* store,
                       std::filesystem::path storePath,
                       QWidget* parent)
    : QMainWindow(parent),
      store_(store),
      controller_(new ProjectController(store, std::move(storePath), this)),
      sidebar_(new ProjectSidebar(controller_, this)),
      repoController_(new RepoController(this)),
      changesView_(new ChangesView(this)),
      dashboardModel_(new DashboardModel(this)),
      centralStack_(new QStackedWidget(this)) {
    setWindowTitle(QStringLiteral("GitGUI"));

    // Left dock
    auto* dock = new QDockWidget(QStringLiteral("Projects"), this);
    dock->setObjectName(QStringLiteral("projectsDock"));
    dock->setWidget(sidebar_);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Central stack
    centralStack_->setObjectName(QStringLiteral("centralStack"));
    auto* noProjectsPage = makeNoProjectsPage(this);
    auto* noReposPage    = makeNoReposPage(this);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));
    tabs->addTab(changesView_, QStringLiteral("Changes"));
    tabs->addTab(new QLabel(QStringLiteral("History — Plan 4")), QStringLiteral("History"));
    auto* dashboardView = new QListView(this);
    dashboardView->setObjectName(QStringLiteral("dashboardList"));
    dashboardView->setModel(dashboardModel_);
    tabs->addTab(dashboardView, QStringLiteral("Dashboard"));

    centralStack_->addWidget(noProjectsPage); // index 0
    centralStack_->addWidget(noReposPage);    // index 1
    centralStack_->addWidget(tabs);           // index 2
    setCentralWidget(centralStack_);

    // Wire existing repo/sidebar connections (unchanged from before)
    connect(sidebar_, &ProjectSidebar::openInNewWindowRequested,
            this, &MainWindow::openInNewWindowRequested);
    connect(sidebar_, &ProjectSidebar::repoSelected, this, [this](const QString& path) {
        repoController_->open(path);
    });
    connect(repoController_, &RepoController::repoOpened, this, [this](const QString& path) {
        emit repoOpened(path);
        QCoro::connect(repoController_->refreshStatus(), this, [] {});
    });
    connect(repoController_, &RepoController::statusChanged,
            changesView_, &ChangesView::setStatus);
    connect(repoController_, &RepoController::diffReady, this,
            [this](const QString& path, const gitgui::DiffResult& result) {
                changesView_->setDiff(result, std::filesystem::path(path.toStdString()));
            });
    connect(changesView_, &ChangesView::fileSelected, this,
            [this](const QString& path, gitgui::DiffTarget target) {
                QCoro::connect(repoController_->refreshDiff(path, target), this, [] {});
            });
    connect(changesView_, &ChangesView::stageRequested, this,
            [this](const gitgui::StageSelection& sel) {
                QCoro::connect(repoController_->stage(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::unstageRequested, this,
            [this](const gitgui::StageSelection& sel) {
                QCoro::connect(repoController_->unstage(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::discardRequested, this,
            [this](const gitgui::StageSelection& sel) {
                QCoro::connect(repoController_->discard(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::commitRequested, this,
            [this](const gitgui::CommitRequest& req) {
                QCoro::connect(repoController_->commit(req), this, [] {});
            });
    connect(controller_, &ProjectController::projectActivated, this,
            [this](const QString&) {
                QCoro::connect(dashboardModel_->refreshAsync(controller_->activeRepos()),
                               this, [] {});
            });

    // Empty-state page switching
    connect(controller_, &ProjectController::projectActivated,
            this, &MainWindow::updateCentralPage);
    connect(controller_, &ProjectController::projectCreated,
            this, &MainWindow::updateCentralPage);
    connect(controller_, &ProjectController::repoAdded,
            this, &MainWindow::updateCentralPage);

    // Sidebar mutation signals → handlers
    connect(sidebar_, &ProjectSidebar::createProjectRequested,
            this, &MainWindow::onCreateProjectRequested);
    connect(sidebar_, &ProjectSidebar::addExistingRequested,
            this, &MainWindow::onAddExistingRequested);
    connect(sidebar_, &ProjectSidebar::initRepoRequested,
            this, &MainWindow::onInitRepoRequested);
    connect(sidebar_, &ProjectSidebar::cloneRepoRequested,
            this, &MainWindow::onCloneRepoRequested);

    // CTA buttons on the no-projects and no-repos pages
    connect(noProjectsPage->findChild<QPushButton*>(QStringLiteral("createProjectCta")),
            &QPushButton::clicked, this, &MainWindow::onCreateProjectRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("addExistingCta")),
            &QPushButton::clicked, this, &MainWindow::onAddExistingRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("initRepoCta")),
            &QPushButton::clicked, this, &MainWindow::onInitRepoRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("cloneCta")),
            &QPushButton::clicked, this, &MainWindow::onCloneRepoRequested);

    updateCentralPage();
}

void MainWindow::updateCentralPage() {
    if (store_->projects().empty()) {
        centralStack_->setCurrentIndex(0);
    } else if (controller_->activeRepos().empty()) {
        centralStack_->setCurrentIndex(1);
    } else {
        centralStack_->setCurrentIndex(2);
    }
}

void MainWindow::onCreateProjectRequested() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New Project"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        controller_->createProject(name.trimmed());
    }
}

void MainWindow::onAddExistingRequested() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Git Repository"));
    if (dir.isEmpty()) return;
    controller_->addExistingRepo(dir);
    // On failure, repoAddFailed signal is emitted — wire error display in the future.
}

void MainWindow::onInitRepoRequested() {
    InitRepoDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (dlg.parentDir().isEmpty() || dlg.repoName().isEmpty()) return;
    controller_->initRepo(dlg.parentDir(), dlg.repoName());
}

void MainWindow::onCloneRepoRequested() {
    // Stub — implemented in Task 5.
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gitgui::ui
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gitgui_ui_tests --output-on-failure
```

Expected: all UI tests pass including the 3 new empty-state tests.

Check that the old test `show_project_activates_and_lists_repos` still passes — it checks `tabs->count() == 3` where `tabs` is found by objectName `mainTabs`. This is now on stack index 2. The test activates project "id-a" which has 1 repo, so `updateCentralPage()` sets index 2. `mainTabs` is still findable via `findChild`. ✓

- [ ] **Step 8: Commit**

```bash
git add ui/include/gitgui/ui/MainWindow.hpp \
        ui/src/MainWindow.cpp \
        ui/include/gitgui/ui/AddRepoDialogs.hpp \
        ui/src/AddRepoDialogs.cpp \
        ui/CMakeLists.txt \
        tests/ui/test_main_window.cpp
git commit -m "feat(ui): MainWindow empty states + add-existing / init-repo wiring"
```

---

## Task 5: cloneRepo async + CloneRepoDialog + progress wiring

**Files:**
- Modify: `ui/include/gitgui/ui/ProjectController.hpp`
- Modify: `ui/src/ProjectController.cpp`
- Modify: `ui/include/gitgui/ui/AddRepoDialogs.hpp` (add `CloneRepoDialog`)
- Modify: `ui/src/AddRepoDialogs.cpp` (implement `CloneRepoDialog`)
- Modify: `ui/src/MainWindow.cpp` (implement `onCloneRepoRequested`)
- Modify: `tests/ui/test_project_controller.cpp` (2 new clone tests)

**Interfaces:**
- Produces:
  ```cpp
  // ProjectController new additions
  signals:
    void cloneProgress(int received, int total);
  public slots:
    QCoro::Task<void> cloneRepo(QString url, QString dest);
    void cancelClone();
  private:
    std::atomic<bool> cloneCancel_{false};

  // CloneRepoDialog (AddRepoDialogs.hpp)
  class CloneRepoDialog : public QDialog {
      Q_OBJECT
  public:
      explicit CloneRepoDialog(QWidget* parent = nullptr);
      QString url() const;
      QString dest() const;
  };
  ```

- [ ] **Step 1: Write failing tests**

Append to `tests/ui/test_project_controller.cpp` (inside `TestProjectController` class):

```cpp
    void cloneRepo_file_url_succeeds_and_emits_repoAdded() {
        // Create a source repo with one commit so transfer_progress fires
        auto srcDir = std::filesystem::temp_directory_path() /
                      ("gitgui-pc-src-" + std::to_string(rand()));
        std::filesystem::create_directories(srcDir);
        git_repository* srcRaw = nullptr;
        git_repository_init(&srcRaw, srcDir.generic_string().c_str(), 0);
        // Config + commit
        git_config* cfg = nullptr;
        git_repository_config(&cfg, srcRaw);
        git_config_set_string(cfg, "user.name", "T");
        git_config_set_string(cfg, "user.email", "t@e.x");
        git_config_free(cfg);
        { std::ofstream(srcDir / "README") << "hello\n"; }
        git_index* idx = nullptr; git_repository_index(&idx, srcRaw);
        git_index_add_bypath(idx, "README");
        git_index_write(idx);
        git_oid treeOid; git_index_write_tree(&treeOid, idx);
        git_tree* tree = nullptr; git_tree_lookup(&tree, srcRaw, &treeOid);
        git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
        git_oid cOid;
        git_commit_create_v(&cOid, srcRaw, "HEAD", sig, sig, nullptr, "init", tree, 0);
        git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
        git_repository_free(srcRaw);

        auto destDir = std::filesystem::temp_directory_path() /
                       ("gitgui-pc-dst-" + std::to_string(rand()));
        std::filesystem::remove_all(destDir);  // clone creates it

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        QCoro::waitFor(controller.cloneRepo(
            QString::fromStdString("file://" + srcDir.generic_string()),
            QString::fromStdString(destDir.generic_string())));

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::filesystem::exists(destDir / ".git"));
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));

        std::filesystem::remove_all(srcDir);
        std::filesystem::remove_all(destDir);
    }

    void cloneRepo_invalid_url_emits_repoAddFailed() {
        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spyAdded(&controller, &ProjectController::repoAdded);
        QSignalSpy spyFailed(&controller, &ProjectController::repoAddFailed);
        QCoro::waitFor(controller.cloneRepo(
            QStringLiteral("file:///no/such/gitgui-repo-notexist"),
            QStringLiteral("/tmp/gitgui-clone-dst-noexist")));

        QCOMPARE(spyAdded.count(), 0);
        QCOMPARE(spyFailed.count(), 1);
        QVERIFY(!spyFailed.at(0).at(0).toString().isEmpty());
    }
```

Add `#include <fstream>` to the test file (likely already present from earlier tests).

Run — expect compile error (no `cloneRepo` slot yet):
```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

- [ ] **Step 2: Add `cloneRepo`, `cancelClone`, `cloneProgress` to `ProjectController.hpp`**

Add to the signals section:
```cpp
    void cloneProgress(int received, int total);
```

Add to public slots:
```cpp
    QCoro::Task<void> cloneRepo(QString url, QString dest);
    void cancelClone();
```

Add to private section:
```cpp
    std::atomic<bool> cloneCancel_{false};
```

Add includes `#include <atomic>` and `#include <qcorotask.h>`.

- [ ] **Step 3: Implement `cloneRepo` and `cancelClone` in `ProjectController.cpp`**

Add `#include <QtConcurrent>` and `#include <core/qcorofuture.h>` (matching `AsyncRepo.cpp`):

```cpp
#include <QtConcurrent>
#include <core/qcorofuture.h>

void ProjectController::cancelClone() {
    cloneCancel_.store(true);
}

QCoro::Task<void> ProjectController::cloneRepo(QString url, QString dest) {
    cloneCancel_.store(false);

    gitgui::ProgressCallback cb = [this](unsigned r, unsigned t) -> bool {
        if (cloneCancel_.load()) return false;
        QMetaObject::invokeMethod(this, [this, r, t] {
            emit cloneProgress(static_cast<int>(r), static_cast<int>(t));
        }, Qt::QueuedConnection);
        return true;
    };

    const std::string urlStr  = url.toStdString();
    const std::filesystem::path destPath(dest.toStdString());

    auto result = co_await QtConcurrent::run(
        [urlStr, destPath, cb = std::move(cb)]() mutable {
            return gitgui::GitRepo::clone(urlStr, destPath, std::move(cb));
        });

    if (!result) {
        if (!cloneCancel_.load()) {
            emit repoAddFailed(QString::fromStdString(result.error().message));
        }
        co_return;
    }

    auto addResult = store_->addRepo(activeId_.toStdString(),
                                     gitgui::RepoRef{.path = dest.toStdString()});
    if (!addResult) {
        emit repoAddFailed(QString::fromStdString(addResult.error().message));
        co_return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(dest);
}
```

- [ ] **Step 4: Add `CloneRepoDialog` to `AddRepoDialogs.hpp`**

```cpp
// Add to ui/include/gitgui/ui/AddRepoDialogs.hpp

class CloneRepoDialog : public QDialog {
    Q_OBJECT
public:
    explicit CloneRepoDialog(QWidget* parent = nullptr);
    QString url() const;
    QString dest() const;

private:
    QLineEdit* urlEdit_;
    QLineEdit* destEdit_;
};
```

- [ ] **Step 5: Implement `CloneRepoDialog` in `AddRepoDialogs.cpp`**

```cpp
CloneRepoDialog::CloneRepoDialog(QWidget* parent)
    : QDialog(parent),
      urlEdit_(new QLineEdit(this)),
      destEdit_(new QLineEdit(this)) {
    setWindowTitle(QStringLiteral("Clone Repository"));

    auto* browse = new QPushButton(QStringLiteral("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Select destination"));
        if (!dir.isEmpty()) destEdit_->setText(dir);
    });

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("URL:"), urlEdit_);

    auto* destRow = new QWidget(this);
    auto* destLayout = new QHBoxLayout(destRow);
    destLayout->setContentsMargins(0, 0, 0, 0);
    destLayout->addWidget(destEdit_);
    destLayout->addWidget(browse);
    form->addRow(QStringLiteral("Destination:"), destRow);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString CloneRepoDialog::url()  const { return urlEdit_->text(); }
QString CloneRepoDialog::dest() const { return destEdit_->text(); }
```

Add required includes (`QFileDialog`, `QHBoxLayout`) to `AddRepoDialogs.cpp` if not already present.

- [ ] **Step 6: Implement `onCloneRepoRequested` in `MainWindow.cpp`**

Replace the stub:

```cpp
void MainWindow::onCloneRepoRequested() {
    CloneRepoDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString url  = dlg.url().trimmed();
    const QString dest = dlg.dest().trimmed();
    if (url.isEmpty() || dest.isEmpty()) return;

    auto* progress = new QProgressDialog(
        QStringLiteral("Cloning…"), QStringLiteral("Cancel"), 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();

    connect(controller_, &ProjectController::cloneProgress, progress,
            [progress](int r, int t) {
                if (t > 0) progress->setValue(r * 100 / t);
            });
    connect(progress, &QProgressDialog::canceled, controller_,
            &ProjectController::cancelClone);

    QCoro::connect(controller_->cloneRepo(url, dest), this,
                   [progress] {
                       progress->close();
                       progress->deleteLater();
                   });
}
```

Add `#include <QProgressDialog>` to `MainWindow.cpp`.

- [ ] **Step 7: Build and run all tests**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build --output-on-failure
```

Expected: 51 core tests + all UI tests pass (including the 2 new clone tests).

- [ ] **Step 8: Commit**

```bash
git add ui/include/gitgui/ui/ProjectController.hpp \
        ui/src/ProjectController.cpp \
        ui/include/gitgui/ui/AddRepoDialogs.hpp \
        ui/src/AddRepoDialogs.cpp \
        ui/src/MainWindow.cpp \
        tests/ui/test_project_controller.cpp
git commit -m "feat(ui): ProjectController::cloneRepo async + CloneRepoDialog + progress modal"
```

---

## Outcome

Added `ProjectController` mutation slots, promoted `RepoListModel` to a tree model, gave `ProjectSidebar` a "New project…" combo item and an add-repo toolbar, added `MainWindow` empty-state onboarding, and init/clone dialogs with a clone-progress modal. Realises the onboarding/management flows in [`spec/product`](../spec/product/product.md).
