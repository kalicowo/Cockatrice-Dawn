cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ServerAutoLoginTests LANGUAGES CXX)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
set(settings_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../libcockatrice_settings/libcockatrice/settings")
add_executable(server_auto_login_test server_auto_login_test.cpp
  "${settings_dir}/servers_settings.cpp" "${settings_dir}/servers_settings.h"
  "${settings_dir}/settings_manager.cpp" "${settings_dir}/settings_manager.h")
set_target_properties(server_auto_login_test PROPERTIES AUTOMOC ON)
target_compile_features(server_auto_login_test PRIVATE cxx_std_20)
target_include_directories(server_auto_login_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libcockatrice_settings")
target_link_libraries(server_auto_login_test PRIVATE Qt6::Core)
add_test(NAME server_auto_login COMMAND server_auto_login_test)
set_tests_properties(server_auto_login PROPERTIES TIMEOUT 15 LABELS ci)
