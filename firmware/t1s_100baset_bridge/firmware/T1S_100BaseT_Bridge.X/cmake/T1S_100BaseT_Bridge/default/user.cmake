# User CMake configuration to fix portable include paths
# This file overrides the generated CMake configuration to ensure 
# include paths work regardless of checkout location

# ---------------------------------------------------------------------------
# Toolchain version override
# Pass -DXCV32_VERSION=vX.YY to cmake configure to select a different XC32.
# Default falls back to whatever toolchain.cmake has hardcoded (v4.60).
# ---------------------------------------------------------------------------
if(DEFINED XCV32_VERSION AND NOT "${XCV32_VERSION}" STREQUAL "")
    set(XC32_BIN "c:/Program Files/Microchip/xc32/${XCV32_VERSION}/bin")
    set(CMAKE_C_COMPILER   "${XC32_BIN}/xc32-gcc.exe"    CACHE FILEPATH "XC32 C compiler"   FORCE)
    set(CMAKE_CXX_COMPILER "${XC32_BIN}/xc32-g++.exe"    CACHE FILEPATH "XC32 C++ compiler" FORCE)
    set(CMAKE_ASM_COMPILER "${XC32_BIN}/xc32-gcc.exe"    CACHE FILEPATH "XC32 ASM compiler" FORCE)
    set(CMAKE_AR           "${XC32_BIN}/xc32-ar.exe"     CACHE FILEPATH "XC32 archiver"      FORCE)
    set(MP_CC              "${XC32_BIN}/xc32-gcc.exe"    CACHE STRING  "" FORCE)
    set(MP_CPPC            "${XC32_BIN}/xc32-g++.exe"    CACHE STRING  "" FORCE)
    set(MP_AS              "${XC32_BIN}/xc32-gcc.exe"    CACHE STRING  "" FORCE)
    set(MP_LD              "${XC32_BIN}/xc32-ld.exe"     CACHE STRING  "" FORCE)
    set(MP_AR              "${XC32_BIN}/xc32-ar.exe"     CACHE STRING  "" FORCE)
    set(MP_BIN2HEX         "${XC32_BIN}/xc32-bin2hex.exe" CACHE STRING "" FORCE)
    set(OBJCOPY            "${XC32_BIN}/xc32-objcopy.exe" CACHE FILEPATH "" FORCE)
    set(OBJDUMP            "${XC32_BIN}/xc32-objdump.exe" CACHE FILEPATH "" FORCE)
    message(STATUS "XC32 Toolchain: ${XCV32_VERSION} (${XC32_BIN})")
else()
    message(STATUS "XC32 Toolchain: default (kein XCV32_VERSION angegeben, nutze toolchain.cmake)")
endif()

# Determine the project source root relative to this CMake file
# This CMake file is located at: cmake/T1S_100BaseT_Bridge/default/user.cmake
# The project root is 3 levels up from here
get_filename_component(PROJECT_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# Override the compile rule function to fix include paths
function(T1S_100BaseT_Bridge_default_default_XC32_compile_rule target)
    set(options
        "-g"
        "${CC_PRE}"
        "-x"
        "c"
        "-c"
        "-mprocessor=ATSAME54P20A"
        "-ffunction-sections"
        "-fdata-sections"
        "-O1"
        "-fno-common"
        "-Wall"
        "-mdfp=${PACK_REPO_PATH}/Microchip/SAME54_DFP/3.8.234")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG"
        PRIVATE "HAVE_CONFIG_H"
        PRIVATE "WOLFSSL_IGNORE_FILE_WARN"
        PRIVATE "XPRJ_default=default")
    
    # Fixed include directories using absolute paths from project source root
    target_include_directories(${target}
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/config/default"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/config/default/library"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/config/default/library/tcpip/src"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/config/default/library/tcpip/src/common"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/packs/ATSAME54P20A_DFP"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/packs/CMSIS"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/packs/CMSIS/CMSIS/Core/Include"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/third_party/wolfssl"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/third_party/wolfssl/wolfssl"
        PRIVATE "${PROJECT_SOURCE_ROOT}"
        PRIVATE "${PACK_REPO_PATH}/ARM/CMSIS/5.8.0/CMSIS/Core/Include")
endfunction()

# Override the C++ compile rule function as well
function(T1S_100BaseT_Bridge_default_default_XC32_compile_cpp_rule target)
    set(options
        "-g"
        "${CC_PRE}"
        "${DEBUGGER_NAME_AS_MACRO}"
        "-mprocessor=ATSAME54P20A"
        "-frtti"
        "-fexceptions"
        "-fno-check-new"
        "-fenforce-eh-specs"
        "-ffunction-sections"
        "-O1"
        "-fno-common"
        "-mdfp=${PACK_REPO_PATH}/Microchip/SAME54_DFP/3.8.234")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG"
        PRIVATE "XPRJ_default=default")
    
    # Fixed include directories using absolute paths from project source root  
    target_include_directories(${target}
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/config/default"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/packs/ATSAME54P20A_DFP"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/packs/CMSIS"
        PRIVATE "${PROJECT_SOURCE_ROOT}/../src/packs/CMSIS/CMSIS/Core/Include"
        PRIVATE "${PROJECT_SOURCE_ROOT}"
        PRIVATE "${PACK_REPO_PATH}/ARM/CMSIS/5.8.0/CMSIS/Core/Include")
endfunction()

message(STATUS "Using portable include paths from user.cmake")
message(STATUS "Project source root: ${PROJECT_SOURCE_ROOT}")

# ---------------------------------------------------------------------------
# LAN866x SOME/IP client (host toolset stack) wired into the firmware build.
# The platform-neutral stack lives in the parent lan866x-tools repo; this
# bridge is vendored under firmware/t1s_100baset_bridge. PROJECT_SOURCE_ROOT
# is the .X dir, so the repo root is 4 levels up. The per-target platform file
# (plat_h3tcpip.c, the six plat.h functions over Harmony TCPIP_UDP_*) lives in
# this project's firmware/src. See PORTING.md.
# ---------------------------------------------------------------------------
get_filename_component(LAN866X_ROOT "${PROJECT_SOURCE_ROOT}/../../../.." ABSOLUTE)

set(LAN866X_STACK_SRCS
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/src/someip-client.c"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/src/someip-gen.c"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/src/someip-pars.c"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/src/someip-timer.c"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/src/someip-transmit.c"
    "${LAN866X_ROOT}/src/someip_stub.c"
    "${LAN866X_ROOT}/src/rcp.c"
    "${PROJECT_SOURCE_ROOT}/../src/plat_h3tcpip.c"
    "${PROJECT_SOURCE_ROOT}/../src/lan866x_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/clickdemo_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/gpio_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/i2c_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/spi_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/sys_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/dncp_cli.c"
    "${PROJECT_SOURCE_ROOT}/../src/ntp_sync.c")

target_sources(T1S_100BaseT_Bridge_default_default_XC32_compile PRIVATE ${LAN866X_STACK_SRCS})
target_include_directories(T1S_100BaseT_Bridge_default_default_XC32_compile PRIVATE
    "${LAN866X_ROOT}/src"
    "${LAN866X_ROOT}/include"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/inc"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/src"
    "${LAN866X_ROOT}/libepmicrochip/libsomeip/stub")

message(STATUS "LAN866x SOME/IP stack root: ${LAN866X_ROOT}")