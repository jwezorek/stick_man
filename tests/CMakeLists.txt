add_executable(selection_regressions
    selection_regressions.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/stick_man.qrc
)
target_link_libraries(selection_regressions PRIVATE stick_man_ui)
set_target_properties(selection_regressions PROPERTIES AUTORCC ON)

foreach(case IN ITEMS
    rectangle partial modifiers tree clipboard drag pin_state rubber_band_directions fast_release release_endpoint pane_structure
    character_hierarchy character_commands character_pane character_inference character_drag
    character_artwork character_artwork_phase2 character_artwork_phase3
    character_delete character_actions character_error
)
    if(WIN32)
        add_test(NAME selection_${case} COMMAND "${CMAKE_COMMAND}" -E env
            "QT_QPA_PLATFORM=offscreen"
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:selection_regressions> ${case})
    else()
        add_test(NAME selection_${case} COMMAND "${CMAKE_COMMAND}" -E env
            "QT_QPA_PLATFORM=offscreen"
            $<TARGET_FILE:selection_regressions> ${case})
    endif()
    set_tests_properties(selection_${case} PROPERTIES TIMEOUT 30)
endforeach()

add_executable(topology_regressions topology_regressions.cpp)
target_link_libraries(topology_regressions PRIVATE stick_man_model)

foreach(case IN ITEMS regenerate collision paste copy remap duplicate json archive)
    if(WIN32)
        add_test(NAME topology_${case} COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:topology_regressions> ${case})
    else()
        add_test(NAME topology_${case} COMMAND $<TARGET_FILE:topology_regressions> ${case})
    endif()
endforeach()

add_executable(character_stage1 character_stage1.cpp)
target_link_libraries(character_stage1 PRIVATE stick_man_core)
add_test(NAME character_stage1 COMMAND $<TARGET_FILE:character_stage1>)

add_executable(character_stage2 character_stage2.cpp)
target_link_libraries(character_stage2 PRIVATE stick_man_model)

add_executable(character_stage3 character_stage3.cpp)
target_link_libraries(character_stage3 PRIVATE stick_man_model)

foreach(stage IN ITEMS 2 3)
    if(WIN32)
        add_test(NAME character_stage${stage} COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:character_stage${stage}>)
    else()
        add_test(NAME character_stage${stage} COMMAND $<TARGET_FILE:character_stage${stage}>)
    endif()
endforeach()

add_executable(character_stage4 character_stage4.cpp)
target_link_libraries(character_stage4 PRIVATE stick_man_core)
add_test(NAME character_stage4 COMMAND $<TARGET_FILE:character_stage4>)

add_executable(appearances appearances.cpp)
target_link_libraries(appearances PRIVATE stick_man_core)
add_test(NAME appearances COMMAND $<TARGET_FILE:appearances>)

add_executable(animation_v2_phase1 animation_v2_phase1.cpp)
target_link_libraries(animation_v2_phase1 PRIVATE stick_man_model)
if(WIN32)
    add_test(NAME animation_v2_phase1 COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:animation_v2_phase1>)
else()
    add_test(NAME animation_v2_phase1 COMMAND $<TARGET_FILE:animation_v2_phase1>)
endif()

add_executable(ik_pins ik_pins.cpp)
target_link_libraries(ik_pins PRIVATE stick_man_core)
add_test(NAME ik_pins COMMAND $<TARGET_FILE:ik_pins>)

add_executable(constraints constraints.cpp)
target_link_libraries(constraints PRIVATE stick_man_core)
add_test(NAME constraints COMMAND $<TARGET_FILE:constraints>)

add_executable(angle_set angle_set.cpp)
target_link_libraries(angle_set PRIVATE stick_man_core)
add_test(NAME angle_set COMMAND $<TARGET_FILE:angle_set>)

add_executable(animation_ui animation_ui.cpp ${PROJECT_SOURCE_DIR}/src/ui/stick_man.qrc)
target_link_libraries(animation_ui PRIVATE stick_man_ui)
set_target_properties(animation_ui PROPERTIES AUTORCC ON)
add_test(NAME animation_ui COMMAND "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen" "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    $<TARGET_FILE:animation_ui>)
set_tests_properties(animation_ui PROPERTIES TIMEOUT 30)

add_executable(constraint_ui constraint_ui.cpp)
target_link_libraries(constraint_ui PRIVATE stick_man_model)
add_test(NAME constraint_ui COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
    $<TARGET_FILE:constraint_ui>)

add_executable(constraint_solver constraint_solver.cpp)
target_link_libraries(constraint_solver PRIVATE stick_man_core)
add_test(NAME constraint_solver COMMAND $<TARGET_FILE:constraint_solver>)
