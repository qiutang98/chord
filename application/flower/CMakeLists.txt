file(GLOB_RECURSE editorHeader CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/flower/*.h")
file(GLOB_RECURSE editorSource CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/flower/*.cpp")

set(editorIconPath "${PROJECT_SOURCE_DIR}/application/flower/icon.rc")

add_executable(flower ${editorHeader} ${editorSource} ${editorIconPath})
set_property(TARGET flower PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_link_libraries(flower PRIVATE Chord::Chord)

set_property(TARGET flower PROPERTY USE_FOLDERS ON)
source_group(TREE "${PROJECT_SOURCE_DIR}/application/flower" FILES ${editorHeader} ${editorSource})

## Add pch to accelerate our compile speed.
target_precompile_headers(flower PRIVATE "${PROJECT_SOURCE_DIR}/application/flower/pch.h")
groupCMakeFiles(flower)

set_target_properties(flower PROPERTIES FOLDER "application")

## CMakeLists and icon move to code folder.
source_group("cmake" FILES "CMakeLists.txt" ${editorIconPath})

# Visual studio incremental build.
if(MSVC)
    target_compile_options(flower PRIVATE "/ZI")
    target_link_options(flower PRIVATE "/INCREMENTAL")   
else()
    message(FATAL_ERROR "Current only test on MSVC.")
endif()

set_target_properties(flower PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/install") 