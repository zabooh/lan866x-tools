cmake_minimum_required(VERSION 3.10)
project(lan866x_tools CXX C)

# ============================================================================
# Self-contained package: all required sources live in this directory
#   include/        -> public headers (lan866x_client.hpp, ...)
#   libepmicrochip/ -> SOME/IP stack + liblan866x + platform stub (Windows)
# ============================================================================

# Static lib 'epmicrochip'. Its CMake uses ${CMAKE_SOURCE_DIR}/include,
# which here points to lan866x-tools/include -> works out of the box.
add_subdirectory(libepmicrochip)

# ----------------------------------------------------------------------------
# Track A (ready to run): endpoint/service discovery, C++ via the factory
# ----------------------------------------------------------------------------
add_executable(lan866x-discovery discovery.cpp)
target_include_directories(lan866x-discovery PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lan866x-discovery PRIVATE epmicrochip)
set_target_properties(lan866x-discovery PROPERTIES CXX_STANDARD 20)
if(WIN32)
    target_compile_definitions(lan866x-discovery PRIVATE WIN32 _WIN32_WINNT=0x0601)
    target_link_libraries(lan866x-discovery PRIVATE ws2_32 Iphlpapi)
endif()

# Static linking with GCC/MinGW -> a single dependency-free .exe
# (no libgcc/libstdc++/libwinpthread DLLs needed).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-discovery PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------------
# Target 2: I2C bus scanner (i2cdetect via an endpoint)
# ----------------------------------------------------------------------------
add_executable(lan866x-i2cscan i2cscan.cpp)
target_include_directories(lan866x-i2cscan PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lan866x-i2cscan PRIVATE epmicrochip)
set_target_properties(lan866x-i2cscan PROPERTIES CXX_STANDARD 20)
if(WIN32)
    target_compile_definitions(lan866x-i2cscan PRIVATE WIN32 _WIN32_WINNT=0x0601)
    target_link_libraries(lan866x-i2cscan PRIVATE ws2_32 Iphlpapi)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-i2cscan PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------------
# Targets 3-6: GPIO, SPI, ADC and PWM tools (same dependency profile)
# ----------------------------------------------------------------------------
foreach(tool gpio spi adc pwm)
    add_executable(lan866x-${tool} ${tool}.cpp)
    target_include_directories(lan866x-${tool} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(lan866x-${tool} PRIVATE epmicrochip)
    set_target_properties(lan866x-${tool} PROPERTIES CXX_STANDARD 20)
    if(WIN32)
        target_compile_definitions(lan866x-${tool} PRIVATE WIN32 _WIN32_WINNT=0x0601)
        target_link_libraries(lan866x-${tool} PRIVATE ws2_32 Iphlpapi)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(lan866x-${tool} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endforeach()

# ----------------------------------------------------------------------------
# Target 7: DNCP monitor (standalone, Winsock only - DNCP != SOME/IP)
# ----------------------------------------------------------------------------
add_executable(lan866x-dncpmon dncpmon.cpp)
set_target_properties(lan866x-dncpmon PROPERTIES CXX_STANDARD 20)
if(WIN32)
    target_compile_definitions(lan866x-dncpmon PRIVATE WIN32 _WIN32_WINNT=0x0601)
    target_link_libraries(lan866x-dncpmon PRIVATE ws2_32)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-dncpmon PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------------
# Target 8: active DNCP discovery (standalone; ws2_32 + iphlpapi)
# ----------------------------------------------------------------------------
add_executable(lan866x-dncpdisc dncpdisc.cpp)
set_target_properties(lan866x-dncpdisc PROPERTIES CXX_STANDARD 20)
if(WIN32)
    target_compile_definitions(lan866x-dncpdisc PRIVATE WIN32 _WIN32_WINNT=0x0601)
    target_link_libraries(lan866x-dncpdisc PRIVATE ws2_32 Iphlpapi)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-dncpdisc PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------------
# Target 9: lan866x-probe-c  -  PURE-C end-to-end proof (Track B running on PC)
#   Built ONLY from C sources: the tool (probe.c), the RCP wrapper (rcp.c), a
#   C platform stub (someip_stub_win.c) reusing windows-udp-handler.c, and the
#   C SOME/IP core (libsomeip/src/*.c). No C++ TU -> links without libstdc++.
# ----------------------------------------------------------------------------
file(GLOB SOMEIP_C_CORE ${CMAKE_CURRENT_SOURCE_DIR}/libepmicrochip/libsomeip/src/*.c)
add_executable(lan866x-probe-c
    src/probe.c
    src/rcp.c
    src/someip_stub_win.c
    libepmicrochip/libsomeip/stub/windows-udp-handler.c
    ${SOMEIP_C_CORE})
target_include_directories(lan866x-probe-c PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libepmicrochip/libsomeip/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/libepmicrochip/libsomeip/stub)
set_target_properties(lan866x-probe-c PROPERTIES C_STANDARD 11 LINKER_LANGUAGE C)
if(WIN32)
    target_compile_definitions(lan866x-probe-c PRIVATE WIN32 _WIN32_WINNT=0x0601)
    target_link_libraries(lan866x-probe-c PRIVATE ws2_32 Iphlpapi)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-probe-c PRIVATE -static -static-libgcc)
endif()

# ----------------------------------------------------------------------------
# Note on the C template (src/main.c, rcp.c, rcp.h):
#   Portable MCU32 template (pure C on libsomeip). The Windows proof above
#   (lan866x-probe-c) builds rcp.c + a C stub directly; for MCU32 you replace
#   someip_stub_win.c with an lwIP/FreeRTOS stub (same SOMEIP_CB_* set). See
#   PORTING.md.
# ----------------------------------------------------------------------------
