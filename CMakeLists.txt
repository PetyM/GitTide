cmake_minimum_required(VERSION 3.28)
project(gittide LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITGUI_BUILD_TESTS "Build tests" ON)
option(GITGUI_BUILD_UI "Build the Qt UI and app" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(core)

if(GITGUI_BUILD_UI)
  add_subdirectory(ui)
  add_subdirectory(app)
endif()

if(GITGUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
