cmake_minimum_required(VERSION 3.10)

set(BIN_FILE ${PROJECT_NAME}.bin)
set(ELF_FILE ${PROJECT_NAME}.elf)

find_program(OBJCOPY arm-none-eabi-objcopy)

if(NOT OBJCOPY)
    message(FATAL_ERROR "arm-none-eabi-objcopy not found!")
endif()

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${OBJCOPY} -O binary ${ELF_FILE} ${BIN_FILE}
    COMMENT "Generating binary file from ELF..."
    VERBATIM
)
