cmake_minimum_required(VERSION 3.16)
project(ucar_timing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt5 or Qt6
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(Qt_LIBRARIES Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(Qt_LIBRARIES Qt5::Widgets)
endif()

add_executable(timing
    main.cpp
    track_widget.cpp
    track_widget.h
    assets.qrc
)

target_link_libraries(timing PRIVATE ${Qt_LIBRARIES})

if(APPLE)
    target_compile_options(timing PRIVATE -Wno-implicit-function-declaration)
endif()