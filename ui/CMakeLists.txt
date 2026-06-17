add_library(gittide_ui STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionStore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectListModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ProjectListModel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/RepoListModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/RepoListModel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ProjectController.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/RepoController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/RepoController.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DashboardModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/DashboardModel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectSidebar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ProjectSidebar.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AddRepoDialogs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/AddRepoDialogs.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/MainWindow.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/WindowManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/WindowManager.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AsyncRepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/AsyncRepo.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DiffView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/DiffView.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ChangesView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ChangesView.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/Theme.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ThemeStyle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ThemeStyle.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ThemeManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ThemeManager.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
)
set_target_properties(gittide_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(gittide_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gittide_ui PUBLIC cxx_std_23)
target_link_libraries(gittide_ui PUBLIC
  gittide_core Qt6::Widgets Qt6::Concurrent Qt6::Svg QCoro6::Core)
