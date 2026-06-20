cmake_minimum_required(VERSION 3.10)
project(lan866x_tools C)

# ============================================================================
# Pure-C toolset. Everything is C: no C++ translation unit is compiled, so the
# tools link without libstdc++. The C++ vendor sources that shipped in the
# Microchip package (liblan866x, librtp, someip-stub.cpp) have been removed -
# only the C SOME/IP core (libepmicrochip/libsomeip/src/*.c) is used, wrapped by
# src/rcp.c and a C platform stub. See libepmicrochip/VENDORED.md for the vendor
# provenance and the list of local changes.
# ============================================================================

# --- shared C core: SOME/IP stack + RCP wrapper + Windows platform stub -----
file(GLOB SOMEIP_C_CORE ${CMAKE_CURRENT_SOURCE_DIR}/libepmicrochip/libsomeip/src/*.c)
#   Single-thread platform layer: the platform-neutral SOME/IP stub
#   (src/someip_stub.c) on top of the narrow plat.h, with one impl per platform
#   (src/plat_win.c). Replaces the old threaded someip_stub_win.c +
#   windows-udp-handler.c (still on disk, no longer built).
add_library(rcpcore STATIC
    src/rcp.c
    src/someip_stub.c
    src/plat_win.c
    ${SOMEIP_C_CORE})
target_include_directories(rcpcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libepmicrochip/libsomeip/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/libepmicrochip/libsomeip/stub)
set_target_properties(rcpcore PROPERTIES C_STANDARD 11)
if(WIN32)
    target_compile_definitions(rcpcore PUBLIC WIN32 _WIN32_WINNT=0x0601)
endif()

# --- RCP tools (SOME/IP): link the C core ----------------------------------
#   Each tool .c defines its own  uint8_t MULTICAST_IP[] = {224,0,0,1};
foreach(tool discovery i2cscan gpio spi adc pwm boot flashimg clickdemo diag video)
    add_executable(lan866x-${tool} ${tool}.c)
    target_link_libraries(lan866x-${tool} PRIVATE rcpcore)
    set_target_properties(lan866x-${tool} PROPERTIES C_STANDARD 11)
    if(WIN32)
        target_link_libraries(lan866x-${tool} PRIVATE ws2_32 Iphlpapi)
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_options(lan866x-${tool} PRIVATE -static -static-libgcc)
    endif()
endforeach()
# clickdemo + diag additionally need winmm for timeBeginPeriod (1 ms scheduler tick)
if(WIN32)
    target_link_libraries(lan866x-clickdemo PRIVATE winmm)
    target_link_libraries(lan866x-diag PRIVATE winmm)
    target_link_libraries(lan866x-video PRIVATE winmm)
endif()

# --- package flasher: needs a ZIP reader (bundled minizip/inflate fork) -----
file(GLOB MINIZIP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third-party/minizip/*.c)
add_library(lan866x_minizip STATIC ${MINIZIP_SRC})
target_include_directories(lan866x_minizip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third-party/minizip)
set_target_properties(lan866x_minizip PROPERTIES C_STANDARD 11)

add_executable(lan866x-flashpkg flashpkg.c)
target_link_libraries(lan866x-flashpkg PRIVATE rcpcore lan866x_minizip)
set_target_properties(lan866x-flashpkg PROPERTIES C_STANDARD 11)
if(WIN32)
    target_link_libraries(lan866x-flashpkg PRIVATE ws2_32 Iphlpapi)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-flashpkg PRIVATE -static -static-libgcc)
endif()

# --- standalone DNCP tools (Winsock only, not SOME/IP) ----------------------
foreach(tool dncpmon dncpdisc)
    add_executable(lan866x-${tool} ${tool}.c)
    set_target_properties(lan866x-${tool} PROPERTIES C_STANDARD 11)
    if(WIN32)
        target_compile_definitions(lan866x-${tool} PRIVATE WIN32 _WIN32_WINNT=0x0601)
        target_link_libraries(lan866x-${tool} PRIVATE ws2_32)
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_options(lan866x-${tool} PRIVATE -static -static-libgcc)
    endif()
endforeach()
target_link_libraries(lan866x-dncpdisc PRIVATE Iphlpapi)
