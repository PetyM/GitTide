add_executable(gitgui_app ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
set_target_properties(gitgui_app PROPERTIES AUTOMOC ON)
target_link_libraries(gitgui_app PRIVATE gitgui_ui)
