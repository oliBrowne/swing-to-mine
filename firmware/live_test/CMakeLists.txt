# Live test: MPU-6050 -> int8 gesture model every 50 ms -> LEDs.
# Uses the same ExecuTorch build, model export and startup/board code as
# ../model_test (see its README for the ExecuTorch steps).
#
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../model_test/arm-gcc-toolchain.cmake
#   cmake --build build
cmake_minimum_required(VERSION 3.20)
project(live_test C CXX ASM)

set(MT ${CMAKE_SOURCE_DIR}/../model_test)
set(ET_ROOT "$ENV{HOME}/executorch" CACHE PATH "folder that contains the executorch/ source checkout")
set(ET_BUILD "$ENV{HOME}/executorch/executorch-build/cortex-m4-cmsis" CACHE PATH
    "ExecuTorch tree from ../model_test/scripts/build_executorch.sh")
set(CMSIS_NN "$ENV{HOME}/executorch/CMSIS-NN" CACHE PATH "CMSIS-NN source")
set(TORCH_INCLUDE "$ENV{HOME}/executorch/et-env/lib/python3.12/site-packages/torch/include" CACHE PATH
    "torch headers from the ExecuTorch Python env")
set(MODEL_DIR "${CMAKE_SOURCE_DIR}/../../training/out/model_test" CACHE PATH
    "folder with model.pte from training/export.py (int8)")

set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

file(READ ${MODEL_DIR}/model.pte PTE_HEX HEX)
string(LENGTH "${PTE_HEX}" PTE_HEX_LEN)
math(EXPR PTE_LEN "${PTE_HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," PTE_BYTES "${PTE_HEX}")
file(WRITE ${CMAKE_BINARY_DIR}/model_pte.h
    "// generated from ${MODEL_DIR}/model.pte\n#pragma once\n"
    "__attribute__((aligned(16))) static const unsigned char model_pte[] = {${PTE_BYTES}};\n"
    "static const unsigned int model_pte_len = ${PTE_LEN};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MODEL_DIR}/model.pte)

add_executable(live_test.elf
    src/main.cpp src/imu.c
    ${MT}/src/startup.c ${MT}/src/board.c ${MT}/src/syscalls.c ${MT}/src/fast_mem.c
    ${MT}/src/op_quantized_avg_pool2d_patched.cpp)
target_compile_options(live_test.elf PRIVATE ${CPU_FLAGS} -O2 -g -ffunction-sections -fdata-sections
    -DET_LOG_ENABLED=0 $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -fno-exceptions -fno-rtti>)
set_source_files_properties(${MT}/src/fast_mem.c PROPERTIES COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_include_directories(live_test.elf PRIVATE
    src ${MT}/src ${CMAKE_BINARY_DIR}
    ${ET_ROOT}
    ${ET_ROOT}/executorch/runtime/core/portable_type/c10
    ${ET_ROOT}/executorch/third-party/flatbuffers/include
    ${ET_ROOT}/executorch/backends/cortex_m/ops
    ${CMSIS_NN}/Include
    ${TORCH_INCLUDE})
target_link_libraries(live_test.elf PRIVATE
    -Wl,--whole-archive ${ET_BUILD}/backends/cortex_m/libcortex_m_ops_lib.a -Wl,--no-whole-archive
    ${ET_BUILD}/backends/cortex_m/libcortex_m_kernels.a
    ${ET_BUILD}/backends/cortex_m/_deps/cmsis_nn-build/libcmsis-nn.a
    ${ET_BUILD}/kernels/portable/cpu/util/libkernels_util_all_deps.a
    -Wl,--whole-archive ${ET_BUILD}/libexecutorch_selected_kernels.a -Wl,--no-whole-archive
    ${ET_BUILD}/kernels/portable/libportable_kernels.a
    ${ET_BUILD}/libexecutorch.a
    ${ET_BUILD}/libexecutorch_core.a
    ${ET_BUILD}/extension/data_loader/libextension_data_loader.a)
target_link_options(live_test.elf PRIVATE ${CPU_FLAGS}
    -T${MT}/stm32f407vg.ld -nostartfiles -specs=nano.specs
    -Wl,--gc-sections -Wl,-Map=live_test.map -lc -lm -lgcc)

add_custom_command(TARGET live_test.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary live_test.elf live_test.bin
    COMMAND ${CMAKE_SIZE} live_test.elf)
