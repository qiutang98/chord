file(GLOB_RECURSE editorHeader CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/editor/*.h")
file(GLOB_RECURSE editorSource CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/editor/*.cpp")

set(editorIconPath "${PROJECT_SOURCE_DIR}/application/editor/icon.rc")

add_executable(editor ${editorHeader} ${editorSource} ${editorIconPath})
set_property(TARGET editor PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_link_libraries(editor PRIVATE Chord::Chord)

set_property(TARGET editor PROPERTY USE_FOLDERS ON)
source_group(TREE "${PROJECT_SOURCE_DIR}/application/editor" FILES ${editorHeader} ${editorSource})

## Add pch to accelerate our compile speed.
target_precompile_headers(editor PRIVATE "${PROJECT_SOURCE_DIR}/application/editor/pch.h")
groupCMakeFiles(editor)

set_target_properties(editor PROPERTIES FOLDER "application")

## CMakeLists and icon move to code folder.
source_group("cmake" FILES "CMakeLists.txt" ${editorIconPath})

# Visual studio incremental build.
if(MSVC)
    target_compile_options(editor PUBLIC "/ZI")
    target_link_options(editor PUBLIC "/INCREMENTAL")   
endif()

set_target_properties(editor PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/install") 