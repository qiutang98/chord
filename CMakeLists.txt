cmake_minimum_required(VERSION 3.18)

## Personal vulkan graphics framework playground.
project(chord)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED on)

## Glob all header and source files in project.
file(GLOB_RECURSE chordHeader CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/source/*.h")
file(GLOB_RECURSE chordSource CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/source/*.cpp")

## Add chord framework.
add_library(chord ${chordHeader} ${chordSource})
set_property(TARGET chord PROPERTY COMPILE_WARNING_AS_ERROR ON)

## Alias name for easy use.
add_library(Chord::Chord ALIAS chord)

## Thirdparty packages.
find_package(Vulkan REQUIRED)
add_subdirectory("${PROJECT_SOURCE_DIR}/external/glfw")

## Link libraries for chord.
target_link_libraries(chord PUBLIC 
    Vulkan::Vulkan
    GLFW::GLFW
)

## Add include directories.
target_include_directories(chord PUBLIC 
    "${PROJECT_SOURCE_DIR}/source"
    "${PROJECT_SOURCE_DIR}/external/include"
)

## Add pch to accelerate our compile speed.
target_precompile_headers(chord PRIVATE "${PROJECT_SOURCE_DIR}/source/pch.h")

## PCH generated files  
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/group.cmake")
groupCMakeFiles(chord)

## Group source files.
source_group(TREE "${PROJECT_SOURCE_DIR}/source" PREFIX "source" FILES ${chordHeader} ${chordSource})

# Visual studio incremental build.
if(MSVC)
    target_compile_options(chord PUBLIC "/ZI")
    target_link_options(chord PUBLIC "/INCREMENTAL")   
endif()

## Our applications generate config.
add_subdirectory(application)

