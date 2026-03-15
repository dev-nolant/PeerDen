cmake_minimum_required(VERSION 3.20)
project(peerdden LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
FetchContent_MakeAvailable(httplib)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

find_package(OpenGL REQUIRED)
find_package(OpenSSL)
target_link_libraries(imgui_lib PUBLIC OpenGL::GL)

set(CORE_SOURCES
    src/core/logger.cpp
    src/core/config.cpp
    src/net/api_client.cpp
)

set(TUN_SOURCES
    src/net/tun_adapter.cpp
    src/net/tun_packet_processor.cpp
    src/net/tunnel_crypto.cpp
    src/net/udp_tunnel.cpp
    src/net/relay_connection.cpp
    src/net/relay_peer_tunnel.cpp
    src/net/coord_client.cpp
    src/net/peer_tunnel_manager.cpp
)
if(APPLE)
    list(APPEND TUN_SOURCES src/net/tun_adapter_mac.cpp)
elseif(WIN32)
    list(APPEND TUN_SOURCES src/net/tun_adapter_win.cpp)
endif()

set(PEERDDEN_SOURCES
    src/main.cpp
    src/app.cpp
    src/texture_cache.cpp
    ${CORE_SOURCES}
    src/theme.cpp
    src/tabs/home_tab.cpp
    src/tabs/community_tab.cpp
    src/tabs/system_tab.cpp
    src/tabs/help_tab.cpp
    ${TUN_SOURCES}
)
if(OpenSSL_FOUND)
    list(APPEND PEERDDEN_SOURCES src/updater.cpp)
endif()
if(WIN32)
    configure_file(${CMAKE_SOURCE_DIR}/peerdden.manifest ${CMAKE_BINARY_DIR}/peerdden.manifest COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/peerdden.rc ${CMAKE_BINARY_DIR}/peerdden.rc COPYONLY)
    list(APPEND PEERDDEN_SOURCES ${CMAKE_BINARY_DIR}/peerdden.rc)
endif()
add_executable(peerdden ${PEERDDEN_SOURCES})
target_include_directories(peerdden PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(peerdden PRIVATE imgui_lib httplib::httplib)
if(OpenSSL_FOUND)
  target_compile_definitions(peerdden PRIVATE TUNNGLE_COORD_TLS=1 CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(peerdden PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  if(WIN32)
    # Bundle OpenSSL DLLs so users don't need to install OpenSSL (works with slproweb, vcpkg, etc.)
    set(OPENSSL_DLL_DIR "")
    if(OPENSSL_ROOT_DIR)
      set(OPENSSL_BIN "${OPENSSL_ROOT_DIR}/bin")
      if(EXISTS "${OPENSSL_BIN}")
        set(OPENSSL_DLL_DIR "${OPENSSL_BIN}")
      endif()
    endif()
    if(NOT OPENSSL_DLL_DIR)
      get_target_property(SSL_LOC OpenSSL::SSL IMPORTED_LOCATION_RELEASE)
      if(NOT SSL_LOC)
        get_target_property(SSL_LOC OpenSSL::SSL IMPORTED_LOCATION)
      endif()
      if(SSL_LOC)
        get_filename_component(OPENSSL_DLL_DIR "${SSL_LOC}" DIRECTORY)
      endif()
    endif()
    if(OPENSSL_DLL_DIR)
      file(GLOB OPENSSL_DLLS "${OPENSSL_DLL_DIR}/libssl*.dll" "${OPENSSL_DLL_DIR}/libcrypto*.dll")
      if(NOT OPENSSL_DLLS)
        get_filename_component(OPENSSL_BIN_ALT "${OPENSSL_DLL_DIR}/../bin" ABSOLUTE)
        if(EXISTS "${OPENSSL_BIN_ALT}")
          file(GLOB OPENSSL_DLLS "${OPENSSL_BIN_ALT}/libssl*.dll" "${OPENSSL_BIN_ALT}/libcrypto*.dll")
        endif()
      endif()
      foreach(_dll ${OPENSSL_DLLS})
        add_custom_command(TARGET peerdden POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:peerdden>
          COMMENT "Copying OpenSSL DLL: ${_dll}"
        )
      endforeach()
    endif()
  endif()
endif()

set(LOGO_PNG "${CMAKE_SOURCE_DIR}/assets/logo.png")
if(EXISTS "${LOGO_PNG}")
    add_custom_command(TARGET peerdden POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LOGO_PNG}" $<TARGET_FILE_DIR:peerdden>/logo.png
        COMMENT "Copying logo.png to output directory"
    )
endif()

set(LOGO_PNG_ICON "${CMAKE_SOURCE_DIR}/assets/logo.png")
set(LOGO_ICO "${CMAKE_SOURCE_DIR}/assets/logo.ico")
set(ICON_PNG "${CMAKE_SOURCE_DIR}/assets/icon.png")
set(ICON_PNG_BUILD "${CMAKE_BINARY_DIR}/icon.png")
set(ICON_ICO_WIN "${CMAKE_BINARY_DIR}/peerdden.ico")

if(EXISTS "${LOGO_PNG_ICON}")
    # Use logo.png: convert to ico for exe, copy as icon.png for GLFW window
    add_custom_command(OUTPUT "${ICON_ICO_WIN}"
        COMMAND python "${CMAKE_SOURCE_DIR}/scripts/convert_icon.py" "${LOGO_PNG_ICON}" "${ICON_ICO_WIN}"
        DEPENDS "${LOGO_PNG_ICON}"
        COMMENT "Converting logo.png to peerdden.ico"
    )
    set(ICON_PNG_BUILD "${LOGO_PNG_ICON}")
elseif(EXISTS "${LOGO_ICO}")
    # Fallback: logo.ico -> copy for exe, extract PNG for window
    add_custom_command(OUTPUT "${ICON_ICO_WIN}" "${ICON_PNG_BUILD}"
        COMMAND ${CMAKE_COMMAND} -E copy "${LOGO_ICO}" "${ICON_ICO_WIN}"
        COMMAND python "${CMAKE_SOURCE_DIR}/scripts/convert_icon.py" "${LOGO_ICO}" "${ICON_PNG_BUILD}"
        DEPENDS "${LOGO_ICO}"
        COMMENT "Using logo.ico for exe icon, extracting icon.png for window"
    )
elseif(EXISTS "${ICON_PNG}")
    add_custom_command(OUTPUT "${ICON_ICO_WIN}"
        COMMAND python "${CMAKE_SOURCE_DIR}/scripts/convert_icon.py" "${ICON_PNG}" "${ICON_ICO_WIN}"
        DEPENDS "${ICON_PNG}"
        COMMENT "Converting icon.png to peerdden.ico"
    )
    set(ICON_PNG_BUILD "${ICON_PNG}")
endif()

if(EXISTS "${LOGO_PNG_ICON}" OR EXISTS "${LOGO_ICO}" OR EXISTS "${ICON_PNG}")
    add_custom_target(icon_ico ALL DEPENDS "${ICON_ICO_WIN}")
    add_dependencies(peerdden icon_ico)
    set_source_files_properties(${CMAKE_BINARY_DIR}/peerdden.rc PROPERTIES
        OBJECT_DEPENDS "${ICON_ICO_WIN}"
    )
    add_custom_command(TARGET peerdden POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ICON_PNG_BUILD}" $<TARGET_FILE_DIR:peerdden>/icon.png
        COMMENT "Copying icon.png to output directory"
    )
endif()

if(WIN32)
    target_link_libraries(peerdden PRIVATE ws2_32 iphlpapi shell32)
    set_target_properties(peerdden PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /MANIFEST:NO")

    set(WINTUN_DLL "${CMAKE_SOURCE_DIR}/wintun.dll")
    if(EXISTS "${WINTUN_DLL}")
        add_custom_command(TARGET peerdden POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINTUN_DLL}" $<TARGET_FILE_DIR:peerdden>/wintun.dll
            COMMENT "Copying wintun.dll to output directory"
        )
    endif()
endif()

if(APPLE)
    add_custom_command(TARGET peerdden POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:peerdden>
        COMMENT "Ad-hoc codesigning peerdden"
    )
endif()
