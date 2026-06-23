set(DEPENDENT_MP_BIN2HEXT1S_100BaseT_Bridge_default_C0wUaQsQ "c:/Program Files/Microchip/xc32/v4.60/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFT1S_100BaseT_Bridge_default_C0wUaQsQ ${CMAKE_CURRENT_LIST_DIR}/../../../../out/T1S_100BaseT_Bridge/default.elf)
set(DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_C0wUaQsQ ${CMAKE_CURRENT_LIST_DIR}/../../../../out/T1S_100BaseT_Bridge)
set(DEPENDENT_BYPRODUCTST1S_100BaseT_Bridge_default_C0wUaQsQ ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_C0wUaQsQ}/${sourceFileNameT1S_100BaseT_Bridge_default_C0wUaQsQ}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_C0wUaQsQ}/${sourceFileNameT1S_100BaseT_Bridge_default_C0wUaQsQ}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXT1S_100BaseT_Bridge_default_C0wUaQsQ} --image ${DEPENDENT_DEPENDENT_TARGET_ELFT1S_100BaseT_Bridge_default_C0wUaQsQ} --image-generated-c ${sourceFileNameT1S_100BaseT_Bridge_default_C0wUaQsQ}.c --image-generated-h ${sourceFileNameT1S_100BaseT_Bridge_default_C0wUaQsQ}.h --image-copy-mode ${modeT1S_100BaseT_Bridge_default_C0wUaQsQ} --image-offset ${addressT1S_100BaseT_Bridge_default_C0wUaQsQ} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_C0wUaQsQ}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFT1S_100BaseT_Bridge_default_C0wUaQsQ})
add_custom_target(
    dependent_produced_source_artifactT1S_100BaseT_Bridge_default_C0wUaQsQ 
    DEPENDS ${DEPENDENT_TARGET_DIRT1S_100BaseT_Bridge_default_C0wUaQsQ}/${sourceFileNameT1S_100BaseT_Bridge_default_C0wUaQsQ}.c
    )
