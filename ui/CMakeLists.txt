add_library(gittide_ui STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sessionstore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/projectlistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/projectlistmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repolistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/repolistmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/projectcontroller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/projectcontroller.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repocontroller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/repocontroller.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/projectsidebar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/projectsidebar.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/addrepodialogs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/addrepodialogs.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/branchdialogs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/branchdialogs.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/mainwindow.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/windowmanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/windowmanager.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/asyncrepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/asyncrepo.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/changedfileslist.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/changedfileslist.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/diffview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/diffview.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/changesview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/changesview.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/changedfilesmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/changedfilesmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/branchbar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/branchbar.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/historymodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/historymodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graphdelegate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/graphdelegate.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/historyview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/historyview.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/theme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/theme.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/themestyle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/themestyle.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/thememanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/thememanager.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qmltheme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/qmltheme.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qmlcontext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/qmlcontext.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/qml/qml.qrc
  ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
)
set_target_properties(gittide_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(gittide_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gittide_ui PUBLIC cxx_std_23)
target_link_libraries(gittide_ui PUBLIC
  gittide_core Qt6::Widgets Qt6::Concurrent Qt6::Svg Qt6::Qml Qt6::Quick Qt6::QuickControls2 QCoro6::Core)
