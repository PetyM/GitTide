add_executable(gittide_app ${CMAKE_CURRENT_SOURCE_DIR}/qml_main.cpp)
set_target_properties(gittide_app PROPERTIES AUTOMOC ON)
target_link_libraries(gittide_app PRIVATE gittide_ui Qt6::Gui Qt6::Qml Qt6::Quick)

# Desktop integration: a .desktop entry (StartupWMClass=gittide ties the running
# window to this entry) and a themed scalable icon, so the WM/dock shows the app
# icon instead of a generic fallback.
# All three rules share the `gittide` install component so the app can be
# installed on its own (`cmake --install build --component gittide`) without
# dragging in the FetchContent dependencies' own install rules — notably
# KSyntaxHighlighting's ksyntaxhighlighter6 CLI, whose RPATH-patch step fails
# because we build it EXCLUDE_FROM_ALL (never linked, so never RPATH-stamped).
include(GNUInstallDirs)
install(TARGETS gittide_app COMPONENT gittide
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/packaging/gittide.desktop COMPONENT gittide
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_SOURCE_DIR}/ui/resources/gittide-icon.svg COMPONENT gittide
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME gittide.svg)
