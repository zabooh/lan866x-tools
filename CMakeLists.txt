cmake_minimum_required(VERSION 3.10)
project(lan866x_tools CXX C)

# ============================================================================
# Eigenständiges Paket: alle nötigen Quellen liegen in diesem Verzeichnis
#   include/        -> öffentliche Header (lan866x_client.hpp, ...)
#   libepmicrochip/ -> SOME/IP-Stack + liblan866x + Plattform-Stub (Windows)
# ============================================================================

# Statische Lib 'epmicrochip'. Deren CMake nutzt ${CMAKE_SOURCE_DIR}/include,
# was hier auf lan866x-tools/include zeigt -> passt out of the box.
add_subdirectory(libepmicrochip)

# ----------------------------------------------------------------------------
# Track A (läuft sofort): Endpoint-/Service-Discovery, C++ über die Factory
# ----------------------------------------------------------------------------
add_executable(lan866x-discovery discovery.cpp)
target_include_directories(lan866x-discovery PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lan866x-discovery PRIVATE epmicrochip)
set_target_properties(lan866x-discovery PROPERTIES CXX_STANDARD 20)
if(WIN32)
    target_compile_definitions(lan866x-discovery PRIVATE WIN32 _WIN32_WINNT=0x0601)
    target_link_libraries(lan866x-discovery PRIVATE ws2_32 Iphlpapi)
endif()

# Statisches Linken bei GCC/MinGW -> eine einzelne, abhängigkeitsfreie .exe
# (keine libgcc/libstdc++/libwinpthread-DLLs nötig).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(lan866x-discovery PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------------
# Target 2: I2C-Bus-Scanner (i2cdetect über einen Endpoint)
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
# Target 3+4: GPIO- und SPI-Tool
# ----------------------------------------------------------------------------
foreach(tool gpio spi)
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
# Target 5: DNCP-Monitor (eigenständig, nur Winsock - DNCP != SOME/IP)
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
# Hinweis zur C-Vorlage (src/main.c, rcp.c, rcp.h):
#   Portable MCU32-Vorlage (reines C auf libsomeip). Wird NICHT hier gebaut,
#   sondern im MCU32-Projekt mit einer lwIP/FreeRTOS-Implementierung der
#   SOMEIP_CB_*-Callbacks (Ersatz für den Windows-Stub). Siehe PORTING.md.
# ----------------------------------------------------------------------------
