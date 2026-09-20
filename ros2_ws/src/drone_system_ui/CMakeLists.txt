cmake_minimum_required(VERSION 3.16)
project(drone_system_ui)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(drone_system_interfaces REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Widgets Svg)

add_executable(fleet_console
  src/fleet_console.cpp
  resources/resources.qrc)

ament_target_dependencies(fleet_console rclcpp std_msgs drone_system_interfaces)
target_link_libraries(fleet_console Qt5::Widgets Qt5::Svg)
target_compile_options(fleet_console PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS fleet_console DESTINATION lib/${PROJECT_NAME})
ament_package()
