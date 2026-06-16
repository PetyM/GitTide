add_library(gitgui_core STATIC
  # sources added in later tasks
)
target_include_directories(gitgui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gitgui_core PUBLIC cxx_std_23)

# Header-only for now; add a dummy source so the static lib links.
target_sources(gitgui_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/_placeholder.cpp)
