add_library(gitgui_ui STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionStore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectListModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/ProjectListModel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/RepoListModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/RepoListModel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/ProjectController.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/RepoController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/RepoController.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DashboardModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/DashboardModel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectSidebar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/ProjectSidebar.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/MainWindow.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/WindowManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/WindowManager.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AsyncRepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/AsyncRepo.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DiffView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/DiffView.hpp
)
set_target_properties(gitgui_ui PROPERTIES AUTOMOC ON)
target_include_directories(gitgui_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gitgui_ui PUBLIC cxx_std_23)
target_link_libraries(gitgui_ui PUBLIC
  gitgui_core Qt6::Widgets Qt6::Concurrent QCoro6::Core)
