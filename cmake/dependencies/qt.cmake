# Qt 6 comes from the system or aqtinstall, NEVER FetchContent. The UI is pure
# Qt Quick/QML (no QWidgets): Gui + Qml + Quick + QuickControls2 render it, Test
# for headless unit tests, Concurrent for off-main-thread git ops.
find_package(Qt6 REQUIRED COMPONENTS Gui Test Concurrent Svg Qml Quick QuickControls2 QuickTest Network)

# Qt's FindWrapOpenGL.cmake puts the legacy AGL framework in the
# WrapOpenGL::WrapOpenGL link interface, which Qt6::Gui pulls in transitively.
# The current Xcode SDK removed AGL, so anything linking Qt Gui (our app,
# tests) fails with "framework 'AGL' not found". We render via Metal and never
# use AGL, so strip it. Covers the combined "-framework AGL" element, the split
# "-framework;AGL" pair, and a bare AGL/AGL.framework entry. No-op where AGL
# isn't present (recent Qt, non-Apple), so it's safe everywhere.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  get_target_property(_ogl WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  message(STATUS "GitTide: WrapOpenGL link libs = [${_ogl}]")
  if(_ogl)
    string(REPLACE "-framework;AGL" "" _ogl "${_ogl}")
    string(REPLACE "-framework AGL" "" _ogl "${_ogl}")
    list(FILTER _ogl EXCLUDE REGEX "(^|/)AGL(\\.framework)?$")
    list(REMOVE_ITEM _ogl "")
    set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_ogl}")
    message(STATUS "GitTide: WrapOpenGL link libs after AGL strip = [${_ogl}]")
  endif()
endif()
