cmake_minimum_required(VERSION 3.16)
project(drone_system_sim)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(drone_system_interfaces REQUIRED)
find_package(drone_system_core REQUIRED)

add_executable(fleet_simulator src/fleet_simulator.cpp)
ament_target_dependencies(fleet_simulator
  rclcpp geometry_msgs drone_system_interfaces drone_system_core)
target_compile_options(fleet_simulator PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS fleet_simulator DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch models worlds DESTINATION share/${PROJECT_NAME})

ament_package()
