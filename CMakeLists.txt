cmake_minimum_required(VERSION 3.16)
project(ComputerInspector CXX)
include(FetchContent)
set(USE_TLS  OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(USE_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
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
if(WIN32)
  set(AGENT_PLATFORM  agent/windows.cpp)
  set(SERVER_PLATFORM server/netinfo_win32.cpp)
else()
  set(AGENT_PLATFORM  agent/linux.cpp)
  set(SERVER_PLATFORM server/netinfo_linux.cpp)
endif()
add_executable(agent
  agent/main.cpp
  agent/metric.cpp
  ${AGENT_PLATFORM}
)
target_link_libraries(agent PRIVATE ixwebsocket)
target_include_directories(agent PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(agent PRIVATE -Wall -Wextra)
if(WIN32)
  target_link_libraries(agent PRIVATE iphlpapi ws2_32)
endif()

add_executable(server 
  server/main.cpp
  server/history.cpp
  server/exedir.cpp
  ${SERVER_PLATFORM}
)
target_link_libraries(server PRIVATE ixwebsocket)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(server PRIVATE -Wall -Wextra)
if(WIN32)
  target_link_libraries(server PRIVATE iphlpapi ws2_32)
endif()
if (MINGW)
  target_link_options(agent  PRIVATE -static)
  target_link_options(server PRIVATE -static)
endif()
