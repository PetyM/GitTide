add_executable(gittide_app ${CMAKE_CURRENT_SOURCE_DIR}/qml_main.cpp)
set_target_properties(gittide_app PROPERTIES AUTOMOC ON)
target_link_libraries(gittide_app PRIVATE gittide_ui Qt6::Gui Qt6::Qml Qt6::Quick)

# Version stamped into the macOS bundle (CFBundleShortVersionString / -Version).
set(GITTIDE_VERSION 0.1.0)

if(APPLE)
  # Build a self-contained GitTide.app: a proper bundle (so it drags into
  # /Applications and shows a Dock icon) instead of a bare Unix executable.
  # The QML lives entirely in Qt resources (ui/qml/qml.qrc), so at runtime the
  # bundle only needs the Qt frameworks + Qt QML plugins — those are copied in
  # by the `deploy_macos` target below, which is kept out of the normal build so
  # incremental dev builds stay fast.
  set(GITTIDE_ICNS ${CMAKE_SOURCE_DIR}/ui/resources/gittide.icns)
  set_source_files_properties(${GITTIDE_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(gittide_app PRIVATE ${GITTIDE_ICNS})

  set_target_properties(gittide_app PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "GitTide"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "GitTide"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.gittide.GitTide"
    MACOSX_BUNDLE_BUNDLE_VERSION "${GITTIDE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${GITTIDE_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "gittide"
    MACOSX_BUNDLE_COPYRIGHT "© GitTide")

  # packaging/macos/macdeploy.py runs macdeployqt, then rewrites the residual
  # absolute Homebrew-Qt references it leaves behind to @rpath and ad-hoc
  # codesigns the result (see that script's docstring for why each step is
  # needed). The outcome is a self-contained, launchable GitTide.app.
  #
  # This runs as part of the default build (ALL), because a plain `cmake --build`
  # relinks the executable against absolute Homebrew-Qt paths and drops the
  # @executable_path rpath — leaving a fresh exe next to the already-fixed bundled
  # frameworks, which loads *two* copies of Qt and fails to start. So every build
  # that touches the exe must be followed by a re-deploy. To avoid paying the
  # ~90 MB macdeployqt copy on no-op / test-only builds, the work is gated behind
  # a stamp file: the custom command depends on the exe and only re-runs when the
  # linker has produced a fresh one. The stamp is touched *last* — after
  # macdeploy.py rewrites the exe in place — so it stays newer than the exe and
  # Ninja sees the command as up-to-date until the next relink.
  find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
    HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
  if(MACDEPLOYQT_EXECUTABLE)
    set(GITTIDE_DEPLOY_STAMP ${CMAKE_CURRENT_BINARY_DIR}/gittide_app.deployed.stamp)
    add_custom_command(
      OUTPUT ${GITTIDE_DEPLOY_STAMP}
      COMMAND python3 ${CMAKE_SOURCE_DIR}/packaging/macos/macdeploy.py
              --app $<TARGET_BUNDLE_DIR:gittide_app>
              --macdeployqt ${MACDEPLOYQT_EXECUTABLE}
              --qmldir ${CMAKE_SOURCE_DIR}/ui/qml
      COMMAND ${CMAKE_COMMAND} -E touch ${GITTIDE_DEPLOY_STAMP}
      DEPENDS $<TARGET_FILE:gittide_app>
              ${CMAKE_SOURCE_DIR}/packaging/macos/macdeploy.py
      USES_TERMINAL
      COMMENT "Bundling Qt into GitTide.app (macdeployqt + fixups + codesign)")
    # ALL, so a plain `cmake --build build` yields a launchable bundle; the stamp
    # keeps it a no-op when the exe has not been relinked.
    add_custom_target(deploy_macos ALL DEPENDS ${GITTIDE_DEPLOY_STAMP})
  endif()

  # `cmake --install build --component gittide --prefix /Applications` copies the
  # finished bundle to /Applications/GitTide.app. We install the .app *directory*
  # verbatim rather than via install(TARGETS ...): that keeps CMake from doing its
  # own install-time RPATH rewriting and re-signing, which would undo the @rpath
  # fixups and invalidate the ad-hoc signature that `deploy_macos` already applied.
  # `deploy_macos` is part of ALL, so a normal build has already populated the
  # bundle by the time this install rule runs.
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/GitTide.app
          DESTINATION . COMPONENT gittide USE_SOURCE_PERMISSIONS)
else()
  # Desktop integration (Linux): a .desktop entry (StartupWMClass=gittide ties the
  # running window to this entry) and a themed scalable icon, so the WM/dock shows
  # the app icon instead of a generic fallback.
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
endif()
