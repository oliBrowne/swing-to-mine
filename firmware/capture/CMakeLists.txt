# Data-capture firmware: MPU-6050 (accel + gyro) -> USART2 text stream.
# Build:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-gcc-toolchain.cmake
#   cmake --build build
# Output: build/capture.elf, build/capture.bin, build/capture.hex
cmake_minimum_required(VERSION 3.16)
project(capture C)

add_executable(capture.elf src/main.c src/startup.c)

set(CPU_FLAGS -mcpu=cortex-m4 -mthumb)
target_compile_options(capture.elf PRIVATE ${CPU_FLAGS} -Os -g -Wall -Wextra -ffunction-sections -fdata-sections
    -ffreestanding -fno-tree-loop-distribute-patterns)  # no C library: stop gcc turning loops into memcpy/memset
target_link_options(capture.elf PRIVATE ${CPU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/stm32f407vg.ld
    -nostartfiles -nostdlib -Wl,--gc-sections -Wl,-Map=capture.map)
target_link_libraries(capture.elf PRIVATE gcc)

# Also make .bin and .hex files (STM32CubeProgrammer can flash either) and print the size.
add_custom_command(TARGET capture.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary capture.elf capture.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex capture.elf capture.hex
    COMMAND ${CMAKE_SIZE} capture.elf)
