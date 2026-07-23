cmake_minimum_required(VERSION 3.28)
project(gittide LANGUAGES CXX)

option(GITGUI_BUILD_TESTS "Build tests" ON)
option(GITGUI_BUILD_UI "Build the Qt Quick/QML UI and app" ON)

include(cmake/compiler.cmake)
include(cmake/dependencies.cmake)

add_subdirectory(core)

if(GITGUI_BUILD_UI)
  add_subdirectory(ui)
  add_subdirectory(app)
endif()

if(GITGUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
