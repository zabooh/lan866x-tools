include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(T1S_100BaseT_Bridge_default_library_list )

# Handle files with suffix s, for group default-XC32
if(T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_assemble)
add_library(T1S_100BaseT_Bridge_default_default_XC32_assemble OBJECT ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_assemble})
    T1S_100BaseT_Bridge_default_default_XC32_assemble_rule(T1S_100BaseT_Bridge_default_default_XC32_assemble)
    list(APPEND T1S_100BaseT_Bridge_default_library_list "$<TARGET_OBJECTS:T1S_100BaseT_Bridge_default_default_XC32_assemble>")

endif()

# Handle files with suffix S, for group default-XC32
if(T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_assembleWithPreprocess)
add_library(T1S_100BaseT_Bridge_default_default_XC32_assembleWithPreprocess OBJECT ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_assembleWithPreprocess})
    T1S_100BaseT_Bridge_default_default_XC32_assembleWithPreprocess_rule(T1S_100BaseT_Bridge_default_default_XC32_assembleWithPreprocess)
    list(APPEND T1S_100BaseT_Bridge_default_library_list "$<TARGET_OBJECTS:T1S_100BaseT_Bridge_default_default_XC32_assembleWithPreprocess>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_compile)
add_library(T1S_100BaseT_Bridge_default_default_XC32_compile OBJECT ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_compile})
    T1S_100BaseT_Bridge_default_default_XC32_compile_rule(T1S_100BaseT_Bridge_default_default_XC32_compile)
    list(APPEND T1S_100BaseT_Bridge_default_library_list "$<TARGET_OBJECTS:T1S_100BaseT_Bridge_default_default_XC32_compile>")

endif()

# Handle files with suffix cpp, for group default-XC32
if(T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_compile_cpp)
add_library(T1S_100BaseT_Bridge_default_default_XC32_compile_cpp OBJECT ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_compile_cpp})
    T1S_100BaseT_Bridge_default_default_XC32_compile_cpp_rule(T1S_100BaseT_Bridge_default_default_XC32_compile_cpp)
    list(APPEND T1S_100BaseT_Bridge_default_library_list "$<TARGET_OBJECTS:T1S_100BaseT_Bridge_default_default_XC32_compile_cpp>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_dependentObject)
add_library(T1S_100BaseT_Bridge_default_default_XC32_dependentObject OBJECT ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_dependentObject})
    T1S_100BaseT_Bridge_default_default_XC32_dependentObject_rule(T1S_100BaseT_Bridge_default_default_XC32_dependentObject)
    list(APPEND T1S_100BaseT_Bridge_default_library_list "$<TARGET_OBJECTS:T1S_100BaseT_Bridge_default_default_XC32_dependentObject>")

endif()

# Handle files with suffix elf, for group default-XC32
if(T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_bin2hex)
add_library(T1S_100BaseT_Bridge_default_default_XC32_bin2hex OBJECT ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_bin2hex})
    T1S_100BaseT_Bridge_default_default_XC32_bin2hex_rule(T1S_100BaseT_Bridge_default_default_XC32_bin2hex)
    list(APPEND T1S_100BaseT_Bridge_default_library_list "$<TARGET_OBJECTS:T1S_100BaseT_Bridge_default_default_XC32_bin2hex>")

endif()


# Main target for this project
add_executable(T1S_100BaseT_Bridge_default_image_C0wUaQsQ ${T1S_100BaseT_Bridge_default_library_list})

if(NOT CMAKE_HOST_WIN32)
    set_target_properties(T1S_100BaseT_Bridge_default_image_C0wUaQsQ PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${T1S_100BaseT_Bridge_default_output_dir}")
endif()
set_target_properties(T1S_100BaseT_Bridge_default_image_C0wUaQsQ PROPERTIES
    OUTPUT_NAME "default"
    SUFFIX ".elf")
target_link_libraries(T1S_100BaseT_Bridge_default_image_C0wUaQsQ PRIVATE ${T1S_100BaseT_Bridge_default_default_XC32_FILE_TYPE_link})

# Add the link options from the rule file.
T1S_100BaseT_Bridge_default_link_rule( T1S_100BaseT_Bridge_default_image_C0wUaQsQ)

# Call bin2hex function from the rule file
T1S_100BaseT_Bridge_default_bin2hex_rule(T1S_100BaseT_Bridge_default_image_C0wUaQsQ)
if(CMAKE_HOST_WIN32)
    add_custom_command(
        TARGET T1S_100BaseT_Bridge_default_image_C0wUaQsQ
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${T1S_100BaseT_Bridge_default_output_dir}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:T1S_100BaseT_Bridge_default_image_C0wUaQsQ> ${T1S_100BaseT_Bridge_default_output_dir}/${T1S_100BaseT_Bridge_default_original_image_name}
        BYPRODUCTS ${T1S_100BaseT_Bridge_default_output_dir}/${T1S_100BaseT_Bridge_default_original_image_name}
        COMMENT "Copying elf to out location")
    set_property(
        TARGET T1S_100BaseT_Bridge_default_image_C0wUaQsQ
        APPEND PROPERTY ADDITIONAL_CLEAN_FILES
        ${T1S_100BaseT_Bridge_default_output_dir}/${T1S_100BaseT_Bridge_default_original_image_name})
endif()

