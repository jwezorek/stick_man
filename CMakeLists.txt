cmake_minimum_required(VERSION 3.16)

project(stick_man VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Boost 1.80 REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB MODEL_SOURCES CONFIGURE_DEPENDS src/model/*.cpp)
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/third-party")

add_library(stick_man_core STATIC
    ${CORE_SOURCES}
    ${THIRD_PARTY_DIR}/miniz.c
)
target_include_directories(stick_man_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${THIRD_PARTY_DIR}"
)
target_compile_definitions(stick_man_core PUBLIC MINIZ_NO_STDIO)
target_link_libraries(stick_man_core PUBLIC Eigen3::Eigen)

add_library(stick_man_model STATIC ${MODEL_SOURCES})
set_target_properties(stick_man_model PROPERTIES AUTOMOC ON)
target_link_libraries(stick_man_model PUBLIC stick_man_core Qt6::Widgets)

add_library(stick_man_ui STATIC ${UI_SOURCES})
set_target_properties(stick_man_ui PROPERTIES AUTOMOC ON)
target_include_directories(stick_man_ui PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(stick_man_ui PUBLIC stick_man_model Qt6::Widgets)

add_executable(stick_man
    src/main.cpp
    src/ui/stick_man.qrc
    resources/stick_man.qrc
)
target_link_libraries(stick_man PRIVATE stick_man_ui)
set_target_properties(stick_man PROPERTIES
    AUTORCC ON
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(WIN32)
    target_sources(stick_man PRIVATE resources/stick_man.rc)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
