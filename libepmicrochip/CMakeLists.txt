FILE(GLOB MyCSources liblan866x/*.cpp libsomeip/src/*.c libsomeip/stub/*.c libsomeip/stub/*.cpp  librtp/*.cpp)
add_library (epmicrochip STATIC ${MyCSources})
target_include_directories (epmicrochip
	PUBLIC
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/liblan866x
	${CMAKE_CURRENT_SOURCE_DIR}/libsomeip/inc
	${CMAKE_CURRENT_SOURCE_DIR}/libsomeip/stub
)
