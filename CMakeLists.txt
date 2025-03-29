cmake_minimum_required(VERSION 3.18)

## Personal vulkan graphics framework playground.
project(chord)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED on)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if(MSVC)
    ## Glob all header and source files in project.
    file(GLOB_RECURSE shaderHeaders CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/install/resource/shader/*.*")
else()
    file(GLOB_RECURSE shaderHeaders CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/install/resource/shader/*.h")
endif()

file(GLOB_RECURSE chordHeader CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/source/*.h")
file(GLOB_RECURSE chordSource CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/source/*.cpp")

## Add chord framework.
add_library(chord ${chordHeader} ${chordSource} ${shaderHeaders})
set_property(TARGET chord PROPERTY COMPILE_WARNING_AS_ERROR ON)
set_property(TARGET chord PROPERTY UNITY_BUILD ON)

set_source_files_properties(
  "${PROJECT_SOURCE_DIR}/source/utils/utils_windows.cpp" 
  "${PROJECT_SOURCE_DIR}/source/shader_compiler/compiler.cpp"
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

## Alias name for easy use.
add_library(Chord::Chord ALIAS chord)

## Thirdparty packages.
find_package(Vulkan REQUIRED)
add_subdirectory("${PROJECT_SOURCE_DIR}/external/glfw")
add_subdirectory("${PROJECT_SOURCE_DIR}/external/lz4/cmake") 
add_subdirectory("${PROJECT_SOURCE_DIR}/external/rttr")
add_subdirectory("${PROJECT_SOURCE_DIR}/external/nativefiledialogs")

## Link libraries for chord.
target_link_libraries(chord PUBLIC 
    Vulkan::Vulkan
    GLFW::GLFW
    lz4_static
    nativefiledialog
    RTTR::Core_Lib
    "${PROJECT_SOURCE_DIR}/external/library/dxcompiler.lib"
    "${PROJECT_SOURCE_DIR}/external/library/metis.lib"
    "${PROJECT_SOURCE_DIR}/external/library/GKlib.lib"
)

## Add include directories.
target_include_directories(chord PUBLIC 
    "${PROJECT_SOURCE_DIR}/source"
    "${PROJECT_SOURCE_DIR}/external/include"
    "${PROJECT_SOURCE_DIR}/install/resource"
    "${PROJECT_SOURCE_DIR}/external/tracy"
)

## Add pch to accelerate our compile speed.
target_precompile_headers(chord PRIVATE "${PROJECT_SOURCE_DIR}/source/pch.h")

## PCH generated files  
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/group.cmake")
groupCMakeFiles(chord)

## Group source files.
source_group(TREE "${PROJECT_SOURCE_DIR}/source" PREFIX "source" FILES ${chordHeader} ${chordSource})
source_group(TREE "${PROJECT_SOURCE_DIR}/install/resource/shader" PREFIX "shader" FILES ${shaderHeaders}) 

if(MSVC)
    ## Visual studio incremental build.
    target_compile_options(chord PRIVATE "/ZI")
    target_link_options(chord PRIVATE "/INCREMENTAL")   

    ## Only include hlsl as text file.
    set_source_files_properties(${shaderHeaders} PROPERTIES HEADER_FILE_ONLY TRUE)

    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

    target_compile_options(chord PRIVATE /bigobj)
else()
    message(FATAL_ERROR "Current only test on MSVC.")
endif()

## Our applications generate config.
add_subdirectory(application)

