cmake_minimum_required(VERSION 3.16)
project(drone_system_core)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(drone_system_interfaces REQUIRED)
find_package(CURL QUIET)
find_package(nlohmann_json QUIET)

add_library(drone_state_machine src/state_machine.cpp)
target_include_directories(drone_state_machine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_options(drone_state_machine PRIVATE -Wall -Wextra -Wpedantic)

add_executable(fleet_manager src/fleet_manager.cpp)
ament_target_dependencies(fleet_manager rclcpp drone_system_interfaces)
target_compile_options(fleet_manager PRIVATE -Wall -Wextra -Wpedantic)

if(CURL_FOUND AND nlohmann_json_FOUND)
  add_executable(grok_advisor src/grok_advisor.cpp)
  ament_target_dependencies(grok_advisor rclcpp std_msgs drone_system_interfaces)
  target_link_libraries(grok_advisor CURL::libcurl nlohmann_json::nlohmann_json)
  target_compile_options(grok_advisor PRIVATE -Wall -Wextra -Wpedantic)
  install(TARGETS grok_advisor DESTINATION lib/${PROJECT_NAME})
else()
  message(WARNING "CURL/nlohmann_json not found: grok_advisor will not be built")
endif()

install(TARGETS drone_state_machine
  EXPORT export_drone_state_machine
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include)
install(TARGETS fleet_manager DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_drone_state_machine HAS_LIBRARY_TARGET)
ament_export_libraries(drone_state_machine)
ament_export_include_directories(include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_state_machine test/test_state_machine.cpp)
  target_link_libraries(test_state_machine drone_state_machine)
endif()

ament_package()
