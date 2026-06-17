add_executable(gittide_app ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
set_target_properties(gittide_app PROPERTIES AUTOMOC ON)
target_link_libraries(gittide_app PRIVATE gittide_ui)
