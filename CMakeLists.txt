cmake_minimum_required(VERSION 3.16)
project(ComputerInspector CXX)
include(FetchContent)
set(USE_TLS  OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(USE_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG        v11.4.5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)
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
target_link_libraries(agent PRIVATE ixwebsocket)
if(WIN32)
  target_link_libraries(agent PRIVATE iphlpapi ws2_32)
endif()
target_include_directories(agent PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(agent PRIVATE -Wall -Wextra)
