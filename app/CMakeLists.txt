add_executable(gittide_app ${CMAKE_CURRENT_SOURCE_DIR}/qml_main.cpp)
set_target_properties(gittide_app PROPERTIES AUTOMOC ON)
target_link_libraries(gittide_app PRIVATE gittide_ui Qt6::Gui Qt6::Qml Qt6::Quick)

# Platform packaging: a self-contained bundle + Qt deploy on macOS, executable +
# .desktop/icon install on Linux. Each lives in its own file under cmake/deploy/;
# they run in this directory scope, so they see the gittide_app target directly.
if(APPLE)
  include(${CMAKE_SOURCE_DIR}/cmake/deploy/macos.cmake)
else()
  include(${CMAKE_SOURCE_DIR}/cmake/deploy/linux.cmake)
endif()
