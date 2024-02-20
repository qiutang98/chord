## Application gltf viewer.

file(GLOB_RECURSE viewerHeader CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/viewer/source/*.h")
file(GLOB_RECURSE viewerSource CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/viewer/source/*.cpp")

set(iconPath "${PROJECT_SOURCE_DIR}/application/viewer/source/icon.rc")

add_executable(viewer ${viewerHeader} ${viewerSource} ${iconPath})

target_link_libraries(viewer PRIVATE chord)

set_property(TARGET viewer PROPERTY USE_FOLDERS ON)
source_group(TREE "${PROJECT_SOURCE_DIR}/application/viewer" FILES ${viewerHeader} ${viewerSource})

## Add pch to accelerate our compile speed.
target_precompile_headers(viewer PRIVATE "${PROJECT_SOURCE_DIR}/application/viewer/source/pch.h")
group_pch(viewer)

set_target_properties(viewer PROPERTIES FOLDER "application")

## CMakeLists move to code folder.
source_group("cmake" FILES "CMakeLists.txt" ${iconPath})

set_target_properties(viewer PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/application/viewer") 