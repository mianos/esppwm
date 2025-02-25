# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(condensor)

# Use environment variable or default OTA target path
if(DEFINED ENV{OTA_TARGET_PATH})
    set(OTA_TARGET_PATH $ENV{OTA_TARGET_PATH})
else()
    set(OTA_TARGET_PATH "/Volumes/files/ota")
endif()

# Automatically determine firmware name from project
set(FIRMWARE_NAME "${PROJECT_NAME}.bin")

# Custom target to copy firmware after building .bin
add_custom_target(copy_firmware ALL
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/${FIRMWARE_NAME}
            ${OTA_TARGET_PATH}/${FIRMWARE_NAME}
    DEPENDS ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Copying firmware (${PROJECT_NAME}.bin) to OTA directory..."
)

