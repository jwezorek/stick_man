cmake_minimum_required(VERSION 3.16)

project(stick_man VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.80 REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

find_package(Eigen3  REQUIRED)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/third-party/miniz")
if(NOT EXISTS "${MINIZ_DIR}/miniz.h")
    set(MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/third-party")
endif()
file(GLOB MINIZ_SOURCES CONFIGURE_DEPENDS "${MINIZ_DIR}/miniz*.c")
if(NOT EXISTS "${MINIZ_DIR}/miniz.h" OR NOT MINIZ_SOURCES)
    message(FATAL_ERROR "Vendored miniz sources were not found under src/core/third-party")
endif()

# The upstream source-tree form of miniz generates this header from its own
# CMake build, while release amalgamations do not need it. Supplying a tiny
# private fallback lets stick_man build either vendored layout directly.
set(MINIZ_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/miniz_generated")
file(MAKE_DIRECTORY "${MINIZ_GENERATED_DIR}")
file(WRITE "${MINIZ_GENERATED_DIR}/miniz_export.h"
    "#pragma once\n#ifndef MINIZ_EXPORT\n#define MINIZ_EXPORT\n#endif\n")

add_executable(stick_man
    src/ui/tools/tool_manager.cpp
    src/ui/tools/zoom_tool.cpp
    src/ui/tools/pan_tool.cpp
    src/ui/tools/add_node_tool.cpp
    src/ui/tools/add_bone_tool.cpp
    src/ui/tools/select_tool_panel.cpp
    src/ui/tools/tool.cpp
    src/ui/tools/animate_tool.cpp
    src/ui/tools/selection_tool.cpp
    src/ui/tools/drag_state.cpp
    src/ui/canvas/scene.cpp
    src/ui/canvas/node_item.cpp
    src/ui/canvas/bone_item.cpp
    src/ui/canvas/skel_item.cpp
    src/ui/canvas/rubber_band.cpp
    src/ui/canvas/canvas_item.cpp
    src/ui/canvas/canvas_manager.cpp
    src/ui/panes/artwork_browser.cpp
    src/ui/panes/tools_pane.cpp
    src/ui/panes/skeleton_pane.cpp
    src/ui/panes/tool_settings_pane.cpp
    src/ui/panes/animation_pane.cpp
    src/ui/panes/skeleton_properties.cpp
    src/ui/panes/node_properties.cpp
    src/ui/panes/bone_properties.cpp
    src/ui/panes/props_box.cpp
    src/ui/panes/main_skeleton_pane.cpp
    src/ui/panes/tree_view.cpp
    src/ui/panes/abstract_skeleton_pane.cpp
    src/ui/panes/animation_skeleton_pane.cpp
    src/core/sm_image.cpp
    src/core/sm_artwork.cpp
    src/core/sm_artwork_io.cpp
    src/core/sm_project.cpp
    src/core/sm_character.cpp
    src/core/sm_skeleton.cpp
    src/core/sm_bone.cpp
    src/core/sm_types.cpp
    src/core/sm_fabrik.cpp
    src/core/sm_visit.cpp
    src/core/sm_animation.cpp
    src/core/sm_object_id.cpp
    src/model/project.cpp
    src/model/commands.cpp
    src/model/handle.cpp

    ${MINIZ_SOURCES}

    src/ui/stick_man.qrc
    resources/stick_man.qrc
    src/ui/stick_man.cpp
    src/ui/panes/properties.cpp
    src/ui/clipboard.cpp
    src/ui/character_actions.cpp
    src/ui/util.cpp

    src/main.cpp
)

target_include_directories(stick_man PRIVATE "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
target_compile_definitions(stick_man PRIVATE MINIZ_NO_STDIO)
target_link_libraries(stick_man PRIVATE Qt6::Widgets  Eigen3::Eigen)
set_target_properties(stick_man PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(WIN32)
    target_sources(stick_man PRIVATE resources/stick_man.rc)
endif()

include(CTest)
if(BUILD_TESTING)
    get_target_property(SELECTION_TEST_SOURCES stick_man SOURCES)
    list(REMOVE_ITEM SELECTION_TEST_SOURCES
        src/main.cpp
        resources/stick_man.qrc
        resources/stick_man.rc
    )
    add_executable(selection_regressions tests/selection_regressions.cpp ${SELECTION_TEST_SOURCES})
    target_include_directories(selection_regressions PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(selection_regressions PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(selection_regressions PRIVATE Qt6::Widgets Eigen3::Eigen)
    foreach(case IN ITEMS rectangle partial modifiers tree clipboard drag rubber_band_directions fast_release release_endpoint
        character_hierarchy character_commands character_pane character_inference character_drag
        character_artwork character_clipboard character_delete character_actions character_error)
        if(WIN32)
            add_test(NAME selection_${case} COMMAND "${CMAKE_COMMAND}" -E env
                "QT_QPA_PLATFORM=offscreen" "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                $<TARGET_FILE:selection_regressions> ${case})
        else()
            add_test(NAME selection_${case} COMMAND "${CMAKE_COMMAND}" -E env
                "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:selection_regressions> ${case})
        endif()
        set_tests_properties(selection_${case} PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(topology_regressions
        tests/topology_regressions.cpp
        src/core/sm_image.cpp
        src/core/sm_artwork.cpp
        src/core/sm_artwork_io.cpp
        src/core/sm_project.cpp
        src/core/sm_character.cpp
        src/core/sm_skeleton.cpp
        src/core/sm_bone.cpp
        src/core/sm_types.cpp
        src/core/sm_fabrik.cpp
        src/core/sm_visit.cpp
        src/core/sm_animation.cpp
        src/core/sm_object_id.cpp
        src/model/project.cpp
        src/model/commands.cpp
        src/model/handle.cpp
        ${MINIZ_SOURCES}
    )
    target_include_directories(topology_regressions PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(topology_regressions PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(topology_regressions PRIVATE Qt6::Widgets Eigen3::Eigen)

    add_executable(character_stage1
        tests/character_stage1.cpp
        src/core/sm_image.cpp
        src/core/sm_artwork.cpp
        src/core/sm_artwork_io.cpp
        src/core/sm_project.cpp
        src/core/sm_character.cpp
        src/core/sm_skeleton.cpp
        src/core/sm_bone.cpp
        src/core/sm_types.cpp
        src/core/sm_fabrik.cpp
        src/core/sm_visit.cpp
        src/core/sm_animation.cpp
        src/core/sm_object_id.cpp
        ${MINIZ_SOURCES}
    )
    target_include_directories(character_stage1 PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(character_stage1 PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(character_stage1 PRIVATE Eigen3::Eigen)
    add_test(NAME character_stage1 COMMAND $<TARGET_FILE:character_stage1>)
    foreach(case IN ITEMS regenerate collision paste copy remap duplicate json archive)
        if(WIN32)
            add_test(NAME topology_${case} COMMAND "${CMAKE_COMMAND}" -E env
                "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                $<TARGET_FILE:topology_regressions> ${case})
        else()
            add_test(NAME topology_${case} COMMAND topology_regressions ${case})
        endif()
    endforeach()
endif()

if(BUILD_TESTING)
    get_target_property(CHARACTER_STAGE2_SOURCES topology_regressions SOURCES)
    list(REMOVE_ITEM CHARACTER_STAGE2_SOURCES tests/topology_regressions.cpp)
    add_executable(character_stage2 tests/character_stage2.cpp ${CHARACTER_STAGE2_SOURCES})
    add_executable(character_stage3 tests/character_stage3.cpp ${CHARACTER_STAGE2_SOURCES})
    target_include_directories(character_stage3 PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(character_stage3 PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(character_stage3 PRIVATE Qt6::Widgets Eigen3::Eigen)
    if(WIN32)
        add_test(NAME character_stage3 COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}" $<TARGET_FILE:character_stage3>)
    else()
        add_test(NAME character_stage3 COMMAND character_stage3)
    endif()
    target_include_directories(character_stage2 PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(character_stage2 PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(character_stage2 PRIVATE Qt6::Widgets Eigen3::Eigen)
    if(WIN32)
        add_test(NAME character_stage2 COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}" $<TARGET_FILE:character_stage2>)
    else()
        add_test(NAME character_stage2 COMMAND character_stage2)
    endif()
endif()

if(BUILD_TESTING)
    get_target_property(CHARACTER_STAGE4_SOURCES character_stage1 SOURCES)
    list(REMOVE_ITEM CHARACTER_STAGE4_SOURCES tests/character_stage1.cpp)
    add_executable(character_stage4 tests/character_stage4.cpp ${CHARACTER_STAGE4_SOURCES})
    target_include_directories(character_stage4 PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(character_stage4 PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(character_stage4 PRIVATE Eigen3::Eigen)
    add_test(NAME character_stage4 COMMAND $<TARGET_FILE:character_stage4>)
endif()

if(BUILD_TESTING)
    add_executable(appearances tests/appearances.cpp ${CHARACTER_STAGE4_SOURCES})
    target_include_directories(appearances PRIVATE src "${MINIZ_DIR}" "${MINIZ_GENERATED_DIR}")
    target_compile_definitions(appearances PRIVATE MINIZ_NO_STDIO)
    target_link_libraries(appearances PRIVATE Eigen3::Eigen)
    add_test(NAME appearances COMMAND $<TARGET_FILE:appearances>)
endif()
