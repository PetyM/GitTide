# Syntax highlighting (KDE Frameworks).
# ECM (KDE's Extra CMake Modules) must be on CMAKE_MODULE_PATH for
# KSyntaxHighlighting's own CMakeLists.txt to find its helpers.
# ECM is added AFTER QCoro is already configured (see qcoro.cmake and the include
# order in dependencies.cmake).
FetchContent_Declare(
  ecm
  GIT_REPOSITORY https://invent.kde.org/frameworks/extra-cmake-modules.git
  GIT_TAG        v6.5.0
  GIT_SHALLOW    TRUE
)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)   # suppress ECM's and KSyntax's own tests
FetchContent_MakeAvailable(ecm)
# KSyntaxHighlighting does find_package(ECM CONFIG) and then *overwrites*
# CMAKE_MODULE_PATH with ECM_MODULE_PATH, which points into ECM's INSTALL layout
# (share/ECM/modules). FetchContent only builds ECM, never installs it, so those
# module dirs don't exist and helper includes like ECMPoQmTools fail with
# "Unknown CMake command". On dev machines a system ECM (e.g. Homebrew) hides the
# problem; clean CI runners have none, so all three platforms break. Install our
# pinned ECM into a build-local prefix so find_package(ECM) resolves a complete,
# correct layout everywhere. ECM is script-only — the nested configure+install is
# quick and compiles nothing. Guarded by the config file so we install only once.
set(_ecm_prefix "${CMAKE_BINARY_DIR}/ecm-prefix")
if(NOT EXISTS "${_ecm_prefix}/share/ECM/cmake/ECMConfig.cmake")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -S "${ecm_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}/ecm-prefix-build"
            "-DCMAKE_INSTALL_PREFIX=${_ecm_prefix}" -DBUILD_TESTING=OFF
    RESULT_VARIABLE _ecm_configure OUTPUT_QUIET)
  if(NOT _ecm_configure EQUAL 0)
    message(FATAL_ERROR "Failed to configure bundled ECM for install (exit ${_ecm_configure})")
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}/ecm-prefix-build"
    RESULT_VARIABLE _ecm_install OUTPUT_QUIET)
  if(NOT _ecm_install EQUAL 0)
    message(FATAL_ERROR "Failed to install bundled ECM (exit ${_ecm_install})")
  endif()
endif()
set(ECM_DIR "${_ecm_prefix}/share/ECM/cmake" CACHE PATH "" FORCE)

# We consume only the KF6::SyntaxHighlighting *library* (gittide_ui links it;
# there is no QML import of its quick plugin). Its bundled GUI examples
# (codeeditor, codepdfprinter, minimaltest) and the ksyntaxhighlighter6 CLI are
# Qt executables we never run, and they fail to link on modern macOS SDKs, which
# dropped the AGL framework KF6's build still references. Skip the examples via
# their ecm_optional_add_subdirectory switches (set before MakeAvailable so the
# subdirs are never added), and drop the CLI target from the default build after.
set(BUILD_codeeditor     OFF CACHE BOOL "" FORCE)
set(BUILD_codepdfprinter OFF CACHE BOOL "" FORCE)
set(BUILD_minimaltest    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  KF6SyntaxHighlighting
  GIT_REPOSITORY https://invent.kde.org/frameworks/syntax-highlighting.git
  GIT_TAG        v6.5.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(KF6SyntaxHighlighting)
if(TARGET ksyntaxhighlighter6)
  set_target_properties(ksyntaxhighlighter6 PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
# FetchContent creates the raw CMake target 'KF6SyntaxHighlighting'.
# When the library is installed and found via find_package the target is
# 'KF6::SyntaxHighlighting' (the EXPORT_NAME alias in the package config).
# Create the alias unconditionally so downstream code can always spell
# KF6::SyntaxHighlighting regardless of how the dep was acquired.
if(TARGET KF6SyntaxHighlighting AND NOT TARGET KF6::SyntaxHighlighting)
  add_library(KF6::SyntaxHighlighting ALIAS KF6SyntaxHighlighting)
endif()
