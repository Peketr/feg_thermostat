# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	zigbee_minimal_thermostat
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_zigbee_minimal_thermostat target,
# which contains the content of the SLC project
include(zigbee_minimal_thermostat.cmake)

add_executable(zigbee_minimal_thermostat
    # Add additional sources here
    "../zigbee_helpers.c"
    "../gui.c"
    "../ntc.c"
)

target_include_directories(zigbee_minimal_thermostat PUBLIC
    # Add additional include paths here
)

target_compile_definitions(zigbee_minimal_thermostat PUBLIC
    # Add additional macros here
)

target_compile_options(zigbee_minimal_thermostat PUBLIC
    # Set additional compiler flags here
)

target_link_options(zigbee_minimal_thermostat PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(zigbee_minimal_thermostat PRIVATE
    slc
)

# Include managed project content if available
include(zigbee_minimal_thermostat_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(zigbee_minimal_thermostat PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET zigbee_minimal_thermostat
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:zigbee_minimal_thermostat>" "$<TARGET_FILE_DIR:zigbee_minimal_thermostat>/$<TARGET_FILE_BASE_NAME:zigbee_minimal_thermostat>.s37"
    #COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:zigbee_minimal_thermostat>" "$<TARGET_FILE_DIR:zigbee_minimal_thermostat>/$<TARGET_FILE_BASE_NAME:zigbee_minimal_thermostat>.hex"
    #COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:zigbee_minimal_thermostat>" "$<TARGET_FILE_DIR:zigbee_minimal_thermostat>/$<TARGET_FILE_BASE_NAME:zigbee_minimal_thermostat>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET zigbee_minimal_thermostat
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()