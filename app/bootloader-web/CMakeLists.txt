cmake_minimum_required(VERSION 3.19)

find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)

message(STATUS "npm found: ${NPM_EXECUTABLE}")

# @TODO: fixed this
execute_process(
    COMMAND ${NPM_EXECUTABLE} i
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    ERROR_VARIABLE BUILD_ERROR
    RESULT_VARIABLE BUILD_RESULT
) # not working now

if(NOT BUILD_RESULT EQUAL 0)
    message(STATUS "bootloader-web build failed: ${BUILD_RESULT} ${BUILD_ERROR}")
endif()
