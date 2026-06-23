# The following functions contains all the flags passed to the different build stages.

set(PACK_REPO_PATH "C:/Users/M91221/.mchp_packs" CACHE PATH "Path to the root of a pack repository.")

function(T1S_100BaseT_Bridge_default_default_XC32_assemble_rule target)
    set(options
        "-g"
        "${ASSEMBLER_PRE}"
        "-mprocessor=ATSAME54P20A"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST},--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,--gdwarf-2,-I${CMAKE_CURRENT_SOURCE_DIR}/../../.."
        "-mdfp=${PACK_REPO_PATH}/Microchip/SAME54_DFP/3.8.234")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "__DEBUG=1")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endfunction()
function(T1S_100BaseT_Bridge_default_default_XC32_assembleWithPreprocess_rule target)
    set(options
        "-x"
        "assembler-with-cpp"
        "-g"
        "${MP_EXTRA_AS_PRE}"
        "${DEBUGGER_NAME_AS_MACRO}"
        "-mdfp=${PACK_REPO_PATH}/Microchip/SAME54_DFP/3.8.234"
        "-mprocessor=ATSAME54P20A"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST},--defsym=__MPLAB_DEBUG=1,--gdwarf-2,--defsym=__DEBUG=1,-I${CMAKE_CURRENT_SOURCE_DIR}/../../..")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__DEBUG=1"
        PRIVATE "XPRJ_default=default")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endfunction()
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
    target_include_directories(${target}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/config/default"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/config/default/library"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/config/default/library/tcpip/src"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/config/default/library/tcpip/src/common"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/packs/ATSAME54P20A_DFP"
        PRIVATE "../src/packs/CMSIS"
        PRIVATE "../src/packs/CMSIS/CMSIS/Core/Include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/third_party/wolfssl"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/third_party/wolfssl/wolfssl"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
        PRIVATE "${PACK_REPO_PATH}/ARM/CMSIS/5.8.0/CMSIS/Core/Include")
endfunction()
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
    target_include_directories(${target}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/config/default"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/packs/ATSAME54P20A_DFP"
        PRIVATE "../src/packs/CMSIS"
        PRIVATE "../src/packs/CMSIS/CMSIS/Core/Include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
        PRIVATE "${PACK_REPO_PATH}/ARM/CMSIS/5.8.0/CMSIS/Core/Include")
endfunction()
function(T1S_100BaseT_Bridge_default_dependentObject_rule target)
    set(options
        "-mprocessor=ATSAME54P20A"
        "-mdfp=${PACK_REPO_PATH}/Microchip/SAME54_DFP/3.8.234")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
endfunction()
function(T1S_100BaseT_Bridge_default_link_rule target)
    set(options
        "-g"
        "${MP_EXTRA_LD_PRE}"
        "${DEBUGGER_OPTION_TO_LINKER}"
        "${DEBUGGER_NAME_AS_MACRO}"
        "-mprocessor=ATSAME54P20A"
        "-O1"
        "-mno-device-startup-code"
        "-Wl,--defsym=__MPLAB_BUILD=1${MP_EXTRA_LD_POST},--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,--defsym=_min_heap_size=98304,--gc-sections,-L${CMAKE_CURRENT_SOURCE_DIR}/../../..,-Map=mem.map,-DROM_LENGTH=0x100000,-DROM_ORIGIN=0x0,--memorysummary,memoryfile.xml"
        "-mdfp=${PACK_REPO_PATH}/Microchip/SAME54_DFP/3.8.234")
    list(REMOVE_ITEM options "")
    target_link_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "XPRJ_default=default")
endfunction()
function(T1S_100BaseT_Bridge_default_bin2hex_rule target)
    add_custom_target(
        T1S_100BaseT_Bridge_default_Bin2Hex ALL
        COMMAND ${MP_BIN2HEX} ${T1S_100BaseT_Bridge_default_image_name}
        WORKING_DIRECTORY ${T1S_100BaseT_Bridge_default_output_dir}
        BYPRODUCTS "${T1S_100BaseT_Bridge_default_output_dir}/${T1S_100BaseT_Bridge_default_image_base_name}.hex"
        COMMENT "Convert build file to .hex")
    add_dependencies(T1S_100BaseT_Bridge_default_Bin2Hex ${target})
endfunction()
