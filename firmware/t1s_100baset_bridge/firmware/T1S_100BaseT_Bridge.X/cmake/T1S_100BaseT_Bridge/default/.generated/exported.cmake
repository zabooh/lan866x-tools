set(DEPENDENT_MP_BIN2HEXT1S_100BaseT_Bridge_default_kzV0V7Zs "c:/Program Files/Microchip/xc32/v4.60/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFT1S_100BaseT_Bridge_default_kzV0V7Zs ${CMAKE_CURRENT_LIST_DIR}/../../../../out/T1S_100BaseT_Bridge/default.elf)
set(DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_kzV0V7Zs ${CMAKE_CURRENT_LIST_DIR}/../../../../out/T1S_100BaseT_Bridge)
set(DEPENDENT_BYPRODUCTST1S_100BaseT_Bridge_default_kzV0V7Zs ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_kzV0V7Zs}/${sourceFileNameT1S_100BaseT_Bridge_default_kzV0V7Zs}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_kzV0V7Zs}/${sourceFileNameT1S_100BaseT_Bridge_default_kzV0V7Zs}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXT1S_100BaseT_Bridge_default_kzV0V7Zs} --image ${DEPENDENT_DEPENDENT_TARGET_ELFT1S_100BaseT_Bridge_default_kzV0V7Zs} --image-generated-c ${sourceFileNameT1S_100BaseT_Bridge_default_kzV0V7Zs}.c --image-generated-h ${sourceFileNameT1S_100BaseT_Bridge_default_kzV0V7Zs}.h --image-copy-mode ${modeT1S_100BaseT_Bridge_default_kzV0V7Zs} --image-offset ${addressT1S_100BaseT_Bridge_default_kzV0V7Zs} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_kzV0V7Zs}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFT1S_100BaseT_Bridge_default_kzV0V7Zs})
add_custom_target(
    dependent_produced_source_artifactT1S_100BaseT_Bridge_default_kzV0V7Zs 
    DEPENDS ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_kzV0V7Zs}/${sourceFileNameT1S_100BaseT_Bridge_default_kzV0V7Zs}.c
    )
