cmake_minimum_required(VERSION 3.16)
project(cathook-loader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS
    Core
    Gui
    Widgets
    REQUIRED)

add_executable(cathook-loader
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(cathook-loader
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
) 