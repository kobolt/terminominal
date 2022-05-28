cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(test_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/char.arm.o
                   MAIN_DEPENDENCY char.rom
                   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                   COMMAND ${CMAKE_OBJCOPY}
                   ARGS -I binary -O elf32-littlearm -B armv6s-m char.rom ${CMAKE_CURRENT_BINARY_DIR}/char.arm.o )

add_executable(terminominal char.arm.o)

pico_generate_pio_header(terminominal ${CMAKE_CURRENT_LIST_DIR}/palvideo.pio)
pico_generate_pio_header(terminominal ${CMAKE_CURRENT_LIST_DIR}/ps2kbd.pio)

target_sources(terminominal PRIVATE
        main_pico.c
        palvideo.c
        ps2kbd.c
        eia_pico.c
        terminal.c
        error.c
        )

target_link_libraries(terminominal PRIVATE
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma
        )

pico_add_extra_outputs(terminominal)

target_compile_definitions(terminominal PRIVATE -DKEYBOARD_NORWEGIAN)

