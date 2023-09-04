cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM "rp2040")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

# Pull in SDK (must be before project)
include(3rdparty/pico-sdk/pico_sdk_init.cmake)

set(PROJECT i2c_puppet)

project(${PROJECT} C CXX)

pico_sdk_init()

# Flashloader setup
set(FLASHLOADER_DIR ${CMAKE_CURRENT_LIST_DIR}/3rdparty/pico-flashloader)

# Need at least SDK v1.3.0 (due to https://github.com/raspberrypi/pico-sdk/issues/573)
if(${PICO_SDK_VERSION_STRING} VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Pico SDK v1.3.0 or greater is required.  You have ${PICO_SDK_VERSION_STRING}")
endif()

################################################################################
# Helper function
function(set_linker_script TARGET script)
    target_link_directories(${TARGET} PRIVATE ${FLASHLOADER_DIR})
    pico_set_linker_script(${TARGET} ${FLASHLOADER_DIR}/${script})

    # Add dependencies on the 'included' linker scripts so that the target gets
    # rebuilt if they are changed
    pico_add_link_depend(${TARGET} ${FLASHLOADER_DIR}/memmap_defines.ld)
    pico_add_link_depend(${TARGET} ${FLASHLOADER_DIR}/memmap_default.ld)
endfunction()

################################################################################
# Flashloader
set(FLASHLOADER pico-flashloader)

add_executable(${FLASHLOADER})

target_sources(${FLASHLOADER} PUBLIC
        ${FLASHLOADER_DIR}/flashloader.c
        )

target_link_libraries(${FLASHLOADER} PRIVATE
        hardware_structs
        hardware_sync
        hardware_flash
        hardware_watchdog
        hardware_resets
        hardware_xosc
        hardware_clocks
        hardware_pll
        hardware_dma
        pico_platform
        pico_standard_link
        pico_divider
        )

pico_add_uf2_output(${FLASHLOADER})
pico_set_program_name(${FLASHLOADER} ${FLASHLOADER})
target_compile_options(${FLASHLOADER} PRIVATE -Wall -Wextra -Wno-ignored-qualifiers -Os)

# Use a separate linker script for the flashloader to make sure it is built
# to run at the right location and cannot overflow into the applications's
# address space
set_linker_script(${FLASHLOADER} memmap_flashloader.ld)

set(FLASHLOADER_UF2 ${CMAKE_CURRENT_BINARY_DIR}/${FLASHLOADER}.uf2)

# Application setup

add_subdirectory(app)

target_include_directories(firmware PRIVATE ${FLASHLOADER_DIR})

# Use a separate linker script for the application to make sure it is built
# to run at the right location (after the flashloader).
set_linker_script(firmware memmap_application.ld)

set(FIRMWARE_UF2 ${CMAKE_CURRENT_BINARY_DIR}/app/firmware.uf2)

################################################################################
# Combine the flashloader and application into one flashable UF2 image
set(COMPLETE_UF2 ${CMAKE_CURRENT_BINARY_DIR}/i2c_puppet.uf2)

find_package (Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${COMPLETE_UF2} DEPENDS ${FLASHLOADER} firmware
        COMMENT "Building full UF2 image"
        COMMAND ${Python3_EXECUTABLE}
                ${FLASHLOADER_DIR}/uf2tool.py
                -o ${COMPLETE_UF2} ${FLASHLOADER_UF2} ${FIRMWARE_UF2}
        )

add_custom_target(${PROJECT} ALL DEPENDS ${COMPLETE_UF2})

install(FILES ${COMPLETE_UF2} DESTINATION ${CMAKE_INSTALL_PREFIX} )
