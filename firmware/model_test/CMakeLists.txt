# Model speed test: runs the exported gesture model on the board and times it.
# Needs a cross-built ExecuTorch tree (scripts/build_executorch.sh) and the
# files from training/export.py. Full steps: README.md.
#
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-gcc-toolchain.cmake
#   cmake --build build
cmake_minimum_required(VERSION 3.20)
project(model_test C CXX ASM)

set(ET_ROOT "$ENV{HOME}/executorch" CACHE PATH
    "folder that contains the executorch/ source checkout (ExecuTorch 1.3.0)")
set(ET_BUILD "$ENV{HOME}/executorch/executorch-build/cortex-m4-cmsis" CACHE PATH
    "ExecuTorch tree built by scripts/build_executorch.sh")
set(CMSIS_NN "$ENV{HOME}/executorch/CMSIS-NN" CACHE PATH "CMSIS-NN source (headers for the patched op)")
set(MODEL_DIR "${CMAKE_SOURCE_DIR}/../../training/out/model_test" CACHE PATH
    "folder with model.pte and test_windows.h from training/export.py")
set(TORCH_INCLUDE "$ENV{HOME}/executorch/et-env/lib/python3.12/site-packages/torch/include" CACHE PATH
    "torch headers from the ExecuTorch Python env (ExecuTorch's c10 needs one generated header)")
option(CORTEX_M"model uses the int8 cortex_m (CMSIS-NN) ops" ON)

# -O2, hard float. Note: no -mno-unaligned-access anywhere (see README).
set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
set(COMMON_FLAGS ${CPU_FLAGS} -O2 -g -ffunction-sections -fdata-sections -DET_LOG_ENABLED=0)

# Embed the .pte as a 16-byte aligned const array.
file(READ ${MODEL_DIR}/model.pte PTE_HEX HEX)
string(LENGTH "${PTE_HEX}" PTE_HEX_LEN)
math(EXPR PTE_LEN "${PTE_HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," PTE_BYTES "${PTE_HEX}")
file(WRITE ${CMAKE_BINARY_DIR}/model_pte.h
    "// generated from ${MODEL_DIR}/model.pte\n#pragma once\n"
    "__attribute__((aligned(16))) static const unsigned char model_pte[] = {${PTE_BYTES}};\n"
    "static const unsigned int model_pte_len = ${PTE_LEN};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MODEL_DIR}/model.pte)

add_executable(model_test.elf
    src/startup.c src/board.c src/syscalls.c src/fast_mem.c src/main.cpp)
target_compile_options(model_test.elf PRIVATE ${COMMON_FLAGS}
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -fno-exceptions -fno-rtti>)
# fast_mem.c defines memcpy/memset with loops: stop GCC turning them into calls to themselves.
set_source_files_properties(src/fast_mem.c PROPERTIES COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_include_directories(model_test.elf PRIVATE
    src ${CMAKE_BINARY_DIR} ${MODEL_DIR}
    ${ET_ROOT}
    ${ET_ROOT}/executorch/runtime/core/portable_type/c10
    ${ET_ROOT}/executorch/third-party/flatbuffers/include
    ${TORCH_INCLUDE})   # last: only for torch/headeronly/macros/cmake_macros.h

set(ET_LIBS "")
if(CORTEX_M)
    # Replaces the library's quantized_avg_pool2d (it gives CMSIS-NN no scratch buffer on the M4).
    target_sources(model_test.elf PRIVATE src/op_quantized_avg_pool2d_patched.cpp)
    target_include_directories(model_test.elf PRIVATE
        ${ET_ROOT}/executorch/backends/cortex_m/ops ${CMSIS_NN}/Include)
    list(APPEND ET_LIBS
        -Wl,--whole-archive ${ET_BUILD}/backends/cortex_m/libcortex_m_ops_lib.a -Wl,--no-whole-archive
        ${ET_BUILD}/backends/cortex_m/libcortex_m_kernels.a
        ${ET_BUILD}/backends/cortex_m/_deps/cmsis_nn-build/libcmsis-nn.a)
endif()
list(APPEND ET_LIBS
    ${ET_BUILD}/kernels/portable/cpu/util/libkernels_util_all_deps.a
    -Wl,--whole-archive ${ET_BUILD}/libexecutorch_selected_kernels.a -Wl,--no-whole-archive
    ${ET_BUILD}/kernels/portable/libportable_kernels.a
    ${ET_BUILD}/libexecutorch.a
    ${ET_BUILD}/libexecutorch_core.a
    ${ET_BUILD}/extension/data_loader/libextension_data_loader.a)
target_link_libraries(model_test.elf PRIVATE ${ET_LIBS})
target_link_options(model_test.elf PRIVATE ${CPU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/stm32f407vg.ld -nostartfiles -specs=nano.specs
    -Wl,--gc-sections -Wl,-Map=model_test.map -lc -lm -lgcc)

add_custom_command(TARGET model_test.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary model_test.elf model_test.bin
    COMMAND ${CMAKE_SIZE} model_test.elf)
