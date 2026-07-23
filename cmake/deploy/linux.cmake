# Linux packaging for gittide_app: install the executable plus desktop
# integration (a .desktop entry and a themed scalable icon), so the WM/dock shows
# the app icon instead of a generic fallback. Included from app/CMakeLists.txt in
# the app's directory scope.
#
# StartupWMClass=gittide in the .desktop entry ties the running window to it.
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
