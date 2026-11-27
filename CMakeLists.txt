cmake_minimum_required(VERSION 3.16)
project(ComputerInspector C CXX)   # C: mbedTLS is written in C
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# TLS (wss:// for agents, https:// for the dashboard) through mbedTLS 3.6 LTS (Apache-2.0),
# downloaded and built with the project, so no system OpenSSL is needed on any OS.
set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(GEN_FILES       OFF CACHE BOOL "" FORCE)   # the release archive already has them
set(MBEDTLS_FATAL_WARNINGS     OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON  CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
  URL      https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
  URL_HASH SHA256=8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(mbedtls)
set(MBEDTLS_LIBS mbedtls mbedx509 mbedcrypto)

# IXWebSocket with TLS through that mbedTLS (also on macOS, where it would pick SecureTransport)
set(USE_TLS      ON  CACHE BOOL "" FORCE)
set(USE_MBED_TLS ON  CACHE BOOL "" FORCE)
set(USE_ZLIB     OFF CACHE BOOL "" FORCE)
set(USE_TEST     OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)        # nothing here is installed
set(MBEDTLS_FOUND TRUE)                                  # skip IXWebSocket's find_package
set(MBEDTLS_INCLUDE_DIRS ${mbedtls_SOURCE_DIR}/include)
set(MBEDTLS_LIBRARIES ${MBEDTLS_LIBS})
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG        v11.4.5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)
# set by IXWebSocket's find_package path, which is skipped above
target_compile_definitions(ixwebsocket PRIVATE IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(WIN32)
  set(AGENT_PLATFORM  agent/windows.cpp)
  set(SERVER_PLATFORM server/netinfo_win32.cpp)
elseif(APPLE)
  # macOS has no /proc; the server's netinfo (getifaddrs) works on both
  set(AGENT_PLATFORM  agent/macos.cpp)
  set(SERVER_PLATFORM server/netinfo_linux.cpp)
else()
  set(AGENT_PLATFORM  agent/linux.cpp)
  set(SERVER_PLATFORM server/netinfo_linux.cpp)
endif()
# warning flags: GCC / Clang / MinGW use -Wall -Wextra, Visual Studio's cl.exe does not know them.
# cl.exe also needs /utf-8 (the Thai strings are UTF-8) and NOMINMAX (windows.h defines
# min/max macros that break std::min / std::max).
if(MSVC)
  set(WARN_FLAGS /W4)
  add_compile_options(/utf-8)
  add_compile_definitions(NOMINMAX)
else()
  set(WARN_FLAGS -Wall -Wextra)
endif()
add_executable(agent
  agent/main.cpp
  agent/metric.cpp
  agent/pin.cpp
  agent/keystore.cpp
  common/tlspin.cpp
  common/keys.cpp
  common/exedir.cpp
  ${AGENT_PLATFORM}
)
target_link_libraries(agent PRIVATE ixwebsocket ${MBEDTLS_LIBS})
target_include_directories(agent PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(agent PRIVATE ${WARN_FLAGS})
if(WIN32)
  target_link_libraries(agent PRIVATE iphlpapi ws2_32)
endif()

add_executable(server
  server/main.cpp
  server/core.cpp
  server/history.cpp
  common/exedir.cpp
  server/tlscert.cpp
  server/agentkeys.cpp
  server/auth.cpp
  common/tlspin.cpp
  common/keys.cpp
  ${SERVER_PLATFORM}
)
target_link_libraries(server PRIVATE ixwebsocket ${MBEDTLS_LIBS})
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(server PRIVATE ${WARN_FLAGS})
if(WIN32)
  target_link_libraries(server PRIVATE iphlpapi ws2_32)
endif()
if (MINGW)
  target_link_options(agent  PRIVATE -static)
  target_link_options(server PRIVATE -static)
endif()

#  server_gui: native window for the server (dear imgui + glfw + opengl 3)
# turn off with -DSERVER_GUI=OFF on machines without a desktop (linux needs x11/wayland dev packages)
option(SERVER_GUI "Build the server_gui window" ON)
if (SERVER_GUI)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
  )
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(glfw imgui)
  find_package(OpenGL REQUIRED)

  # noto sans + noto sans thai (sil ofl 1.1), downloaded once and compiled into the binary
  set(FONT_DIR ${CMAKE_BINARY_DIR}/fonts)
  set(FONT_BASE https://github.com/notofonts/notofonts.github.io/raw/main/fonts)
  set(FONT_LIST
    "font_latin_reg|NotoSans/hinted/ttf/NotoSans-Regular.ttf"
    "font_latin_bold|NotoSans/hinted/ttf/NotoSans-Bold.ttf"
    "font_thai_reg|NotoSansThai/hinted/ttf/NotoSansThai-Regular.ttf"
    "font_thai_bold|NotoSansThai/hinted/ttf/NotoSansThai-Bold.ttf")
  set(FONT_CPP ${FONT_DIR}/fonts.cpp)
  if (NOT EXISTS ${FONT_CPP})
    set(out "// Generated by CMakeLists.txt from Noto fonts (SIL OFL 1.1). Do not edit.\n")
    foreach (entry ${FONT_LIST})
      string(REPLACE "|" ";" parts ${entry})
      list(GET parts 0 sym)
      list(GET parts 1 rel)
      get_filename_component(fname ${rel} NAME)
      set(ttf ${FONT_DIR}/${fname})
      if (NOT EXISTS ${ttf})
        message(STATUS "Downloading ${fname}")
        file(DOWNLOAD ${FONT_BASE}/${rel} ${ttf} STATUS st TLS_VERIFY ON)
        list(GET st 0 code)
        if (NOT code EQUAL 0)
          file(REMOVE ${ttf})
          message(FATAL_ERROR "Could not download ${fname}: ${st}")
        endif()
      endif()
      file(READ ${ttf} hex HEX)
      file(SIZE ${ttf} n)
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
      string(APPEND out "extern const unsigned char ${sym}[]={${hex}};\nextern const unsigned ${sym}_len=${n};\n")
    endforeach()
    file(WRITE ${FONT_CPP} "${out}")
  endif()

  add_executable(server_gui
    server/gui/main.cpp
    server/core.cpp
    server/history.cpp
    common/exedir.cpp
    server/tlscert.cpp
    server/agentkeys.cpp
    server/auth.cpp
    common/tlspin.cpp
    common/keys.cpp
    ${SERVER_PLATFORM}
    ${FONT_CPP}
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(server_gui PRIVATE ${CMAKE_SOURCE_DIR} ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(server_gui PRIVATE ixwebsocket glfw OpenGL::GL ${MBEDTLS_LIBS})
  if (WIN32)
    target_link_libraries(server_gui PRIVATE iphlpapi ws2_32 shell32)
    if (MSVC)
      # no console window behind the GUI; keep main() as the entry point
      set_target_properties(server_gui PROPERTIES WIN32_EXECUTABLE ON)
      target_link_options(server_gui PRIVATE /ENTRY:mainCRTStartup)
    endif()
    if (MINGW)
      # no console window behind the gui; mingw's runtime still calls main()
      set_target_properties(server_gui PROPERTIES WIN32_EXECUTABLE ON)
      target_link_options(server_gui PRIVATE -static)
    endif()
  endif()
endif()
