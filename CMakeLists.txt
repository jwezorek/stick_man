cmake_minimum_required(VERSION 3.16)

project(stick_man VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.80 REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS}) 
endif()

find_package(Eigen3 3.3 REQUIRED)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(stick_man
    src/ui/stick_man.qrc
    src/ui/stick_man.cpp
    src/ui/canvas.cpp
    src/ui/tool_palette.cpp
    src/ui/tools.cpp
    src/ui/move_tool.cpp
    src/ui/properties_pane.cpp
    src/ui/tool_settings_pane.cpp
    src/ui/animation_pane.cpp
    src/ui/util.cpp
    src/core/ik_sandbox.cpp
    src/main.cpp
    src/core/ik_types.cpp
)

target_link_libraries(stick_man PRIVATE Qt6::Widgets  Eigen3::Eigen)

set_target_properties(stick_man PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
