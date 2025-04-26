file(GLOB_RECURSE unitTestHeader CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/unit_test/*.h")
file(GLOB_RECURSE unitTestSource CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/application/unit_test/*.cpp")

add_executable(unit_test ${unitTestHeader} ${unitTestSource})
set_property(TARGET unit_test PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_link_libraries(unit_test PRIVATE Chord::Chord)

set_property(TARGET unit_test PROPERTY USE_FOLDERS ON)
source_group(TREE "${PROJECT_SOURCE_DIR}/application/unit_test" FILES ${unitTestHeader} ${unitTestSource})

## Add pch to accelerate our compile speed.
groupCMakeFiles(unit_test)

set_target_properties(unit_test PROPERTIES FOLDER "application")

## CMakeLists and icon move to code folder.
source_group("cmake" FILES "CMakeLists.txt")