add_library(gittide_ui STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/projectlistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/projectlistmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repolistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/repolistmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/projectcontroller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/projectcontroller.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repocontroller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/repocontroller.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/autherror.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/autherror.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/asyncrepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/asyncrepo.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/changedfilesmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/changedfilesmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/difflinesmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/difflinesmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repoviewmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/repoviewmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/branchlistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/branchlistmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/historylistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/historylistmodel.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graphcolumn.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/graphcolumn.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/theme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/theme.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/thememanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/thememanager.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qmltheme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/qmltheme.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qmlcontext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/qmlcontext.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logging.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/logging.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/qml/qml.qrc
  ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
)
set_target_properties(gittide_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(gittide_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gittide_ui PUBLIC cxx_std_23)
target_link_libraries(gittide_ui PUBLIC
  gittide_core Qt6::Gui Qt6::Concurrent Qt6::Svg Qt6::Qml Qt6::Quick Qt6::QuickControls2 QCoro6::Core)
