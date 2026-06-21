add_executable(gittide_app ${CMAKE_CURRENT_SOURCE_DIR}/qml_main.cpp)
set_target_properties(gittide_app PROPERTIES AUTOMOC ON)
target_link_libraries(gittide_app PRIVATE gittide_ui Qt6::Gui Qt6::Qml Qt6::Quick)
