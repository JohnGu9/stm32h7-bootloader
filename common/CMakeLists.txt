cmake_minimum_required(VERSION 3.21)

set(DEST_FILE "${CMAKE_CURRENT_LIST_DIR}/bootloader-common-config.h")
set(SOURCE_FILE "${CMAKE_CURRENT_LIST_DIR}/bootloader-common-config-template.h")

if(NOT EXISTS "${DEST_FILE}")
    message(STATUS "Config file not found. Copying default template...")
    file(COPY_FILE "${SOURCE_FILE}" "${DEST_FILE}")
endif()

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} common_src)

add_library(bootloader_common OBJECT ${common_src})
target_include_directories(bootloader_common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
