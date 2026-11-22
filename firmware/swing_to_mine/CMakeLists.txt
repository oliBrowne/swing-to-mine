# Swing to Mine controller firmware: USB keyboard + mouse, air mouse, modes,
# and (SWING_MODEL=ON) the int8 gesture model.
#
#   No model (what CI builds; buttons, looking and walking work, no gestures):
#     cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../model_test/arm-gcc-toolchain.cmake
#     cmake --build build
#
#   With the model (needs ExecuTorch, as in ../model_test and ../live_test):
#     cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../model_test/arm-gcc-toolchain.cmake -DSWING_MODEL=ON
#     cmake --build build
#
# Needs the git submodules in ../lib (git submodule update --init).
cmake_minimum_required(VERSION 3.20)
project(swing_to_mine C CXX ASM)

option(SWING_MODEL "link the gesture model with ExecuTorch" OFF)

set(MT ${CMAKE_SOURCE_DIR}/../model_test)
set(LT ${CMAKE_SOURCE_DIR}/../live_test)
set(LIB ${CMAKE_SOURCE_DIR}/../lib)
set(TUSB ${LIB}/tinyusb/src)
set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

if(NOT EXISTS ${TUSB}/tusb.c)
  message(FATAL_ERROR "TinyUSB missing: run `git submodule update --init` in the repo root")
endif()

set(APP_SOURCES
    src/main.c src/controller.c src/button.c src/motion.c src/air_mouse.c
    src/tilt_walk.c src/gesture_decoder.c src/hid_out.c src/usb_descriptors.c src/startup.c
    ${LT}/src/imu.c
    ${MT}/src/board.c ${MT}/src/syscalls.c
    ${TUSB}/tusb.c ${TUSB}/common/tusb_fifo.c ${TUSB}/device/usbd.c
    ${TUSB}/class/hid/hid_device.c
    ${TUSB}/portable/synopsys/dwc2/dcd_dwc2.c ${TUSB}/portable/synopsys/dwc2/dwc2_common.c)

if(SWING_MODEL)
  list(APPEND APP_SOURCES src/model_executorch.cpp ${MT}/src/fast_mem.c ${MT}/src/op_quantized_avg_pool2d_patched.cpp)
else()
  list(APPEND APP_SOURCES src/model_none.c)
endif()

add_executable(swing_to_mine.elf ${APP_SOURCES})
target_include_directories(swing_to_mine.elf PRIVATE
    src ${MT}/src ${LT}/src ${TUSB}
    ${LIB}/cmsis_device_f4/Include ${LIB}/cmsis_core/CMSIS/Core/Include)
target_compile_definitions(swing_to_mine.elf PRIVATE STM32F407xx CFG_TUSB_MCU=OPT_MCU_STM32F4)
target_compile_options(swing_to_mine.elf PRIVATE ${CPU_FLAGS} -O2 -g -Wall -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -fno-exceptions -fno-rtti>)
target_link_options(swing_to_mine.elf PRIVATE ${CPU_FLAGS}
    -T${MT}/stm32f407vg.ld -nostartfiles -specs=nano.specs
    -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=swing_to_mine.map -Wl,--print-memory-usage)
target_link_libraries(swing_to_mine.elf PRIVATE -lc -lm -lgcc)

if(SWING_MODEL)
  # Same ExecuTorch build and model export as ../live_test (see its README).
  set(ET_ROOT "$ENV{HOME}/executorch" CACHE PATH "folder that contains the executorch/ source checkout")
  set(ET_BUILD "$ENV{HOME}/executorch/executorch-build/cortex-m4-cmsis" CACHE PATH
      "ExecuTorch tree from ../model_test/scripts/build_executorch.sh")
  set(CMSIS_NN "$ENV{HOME}/executorch/CMSIS-NN" CACHE PATH "CMSIS-NN source")
  set(TORCH_INCLUDE "$ENV{HOME}/executorch/et-env/lib/python3.12/site-packages/torch/include" CACHE PATH
      "torch headers from the ExecuTorch Python env")
  set(MODEL_DIR "${CMAKE_SOURCE_DIR}/../../training/out/model_test" CACHE PATH
      "folder with model.pte from training/export.py (int8)")

  file(READ ${MODEL_DIR}/model.pte PTE_HEX HEX)
  string(LENGTH "${PTE_HEX}" PTE_HEX_LEN)
  math(EXPR PTE_LEN "${PTE_HEX_LEN} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," PTE_BYTES "${PTE_HEX}")
  file(WRITE ${CMAKE_BINARY_DIR}/model_pte.h
      "// generated from ${MODEL_DIR}/model.pte\n#pragma once\n"
      "__attribute__((aligned(16))) static const unsigned char model_pte[] = {${PTE_BYTES}};\n"
      "static const unsigned int model_pte_len = ${PTE_LEN};\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MODEL_DIR}/model.pte)

  set_source_files_properties(${MT}/src/fast_mem.c PROPERTIES COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
  target_compile_definitions(swing_to_mine.elf PRIVATE ET_LOG_ENABLED=0)
  target_include_directories(swing_to_mine.elf PRIVATE
      ${CMAKE_BINARY_DIR}
      ${ET_ROOT}
      ${ET_ROOT}/executorch/runtime/core/portable_type/c10
      ${ET_ROOT}/executorch/third-party/flatbuffers/include
      ${ET_ROOT}/executorch/backends/cortex_m/ops
      ${CMSIS_NN}/Include
      ${TORCH_INCLUDE})
  target_link_libraries(swing_to_mine.elf PRIVATE
      -Wl,--whole-archive ${ET_BUILD}/backends/cortex_m/libcortex_m_ops_lib.a -Wl,--no-whole-archive
      ${ET_BUILD}/backends/cortex_m/libcortex_m_kernels.a
      ${ET_BUILD}/backends/cortex_m/_deps/cmsis_nn-build/libcmsis-nn.a
      ${ET_BUILD}/kernels/portable/cpu/util/libkernels_util_all_deps.a
      -Wl,--whole-archive ${ET_BUILD}/libexecutorch_selected_kernels.a -Wl,--no-whole-archive
      ${ET_BUILD}/kernels/portable/libportable_kernels.a
      ${ET_BUILD}/libexecutorch.a
      ${ET_BUILD}/libexecutorch_core.a
      ${ET_BUILD}/extension/data_loader/libextension_data_loader.a)
endif()

add_custom_command(TARGET swing_to_mine.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary swing_to_mine.elf swing_to_mine.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex swing_to_mine.elf swing_to_mine.hex
    COMMAND ${CMAKE_SIZE} swing_to_mine.elf)
