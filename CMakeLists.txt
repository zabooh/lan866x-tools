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
# Note on the C template (src/main.c, rcp.c, rcp.h):
#   Portable MCU32 template (pure C on libsomeip). NOT built here, but in the
#   MCU32 project together with an lwIP/FreeRTOS implementation of the
#   SOMEIP_CB_* callbacks (replacing the Windows stub). See PORTING.md.
# ----------------------------------------------------------------------------
