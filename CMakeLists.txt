# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(base-station-rp2040 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
file(GLOB SOURCES
    pico-src/*.h
    pico-src/*.c
    pico-src/*.cpp
)

add_executable(base-station-rp2040 ${SOURCES})

pico_set_program_name(base-station-rp2040 "base-station-rp2040")
pico_set_program_version(base-station-rp2040 "0.1")

# Add the standard include files to the build
target_include_directories(base-station-rp2040 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/pico-src
)

# Add the standard library to the build
target_link_libraries(base-station-rp2040
    pico_stdlib
    tinyusb_device
    tinyusb_board
)

# Add any user requested libraries
target_link_libraries(base-station-rp2040 
    hardware_spi
)

pico_enable_stdio_uart(base-station-rp2040 0)
pico_enable_stdio_usb(base-station-rp2040 1)
pico_add_extra_outputs(base-station-rp2040)

