cmake_minimum_required(VERSION 3.16)
project(linux_control_center VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(linux-control-center src/main.cpp)
target_link_libraries(linux-control-center PRIVATE Qt6::Widgets Qt6::Network)
target_compile_options(linux-control-center PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME application-self-test COMMAND linux-control-center --self-test)
set_tests_properties(application-self-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

install(TARGETS linux-control-center RUNTIME DESTINATION bin)
install(FILES linux-control-center.desktop DESTINATION share/applications)
