cmake_minimum_required(VERSION 3.16)
project(ComputerInspector CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_executable(agent
    agent/main.cpp
    agent/metric.cpp
    agent/linux.cpp
)
target_include_directories(agent PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(agent PRIVATE -Wall -Wextra)
