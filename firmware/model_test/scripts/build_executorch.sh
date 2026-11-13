#!/usr/bin/env bash
# Cross-build the ExecuTorch 1.3.0 libraries the model test links against:
# runtime + portable kernels + the cortex_m (CMSIS-NN) int8 ops.
#
#   bash firmware/model_test/scripts/build_executorch.sh [build_dir]
#
# Assumes the Lab 3 layout: ExecuTorch source in ~/executorch/executorch,
# CMSIS-NN in ~/executorch/CMSIS-NN, Python env in ~/executorch/et-env.
#
# Two things differ from the Lab 3 build and both matter:
#  * -O3 instead of -Os (MinSizeRel).
#  * CMAKE_C_FLAGS/CMAKE_CXX_FLAGS are set here, which drops the
#    -mno-unaligned-access that ExecuTorch's arm-none-eabi.cmake adds. With that
#    flag, CMSIS-NN's 4-byte reads compile to memcpy calls and inference takes
#    21 ms instead of 3.4 ms. The Cortex-M4 handles unaligned loads in hardware.
set -euo pipefail
ET=$HOME/executorch/executorch
B=${1:-$HOME/executorch/executorch-build/cortex-m4-cmsis}
FLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-pic -fno-pie"
# Portable (non-CMSIS) ops the int8 model still needs. For the float model use:
# aten::convolution.out,aten::relu.out,aten::mean.out,aten::permute_copy.out,aten::addmm.out
OPS=${OPS:-aten::view_copy.out}

source "$HOME/executorch/et-env/bin/activate"
cmake -S "$ET" -B "$B" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ET/cmake/toolchains/arm-none-eabi.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DEXECUTORCH_OPTIMIZE_SIZE=OFF \
  -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS" \
  -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -ffunction-sections -fdata-sections" \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections" \
  -DEXECUTORCH_BUILD_KERNELS_PORTABLE=ON -DEXECUTORCH_BUILD_EXTENSION_DATA_LOADER=ON \
  -DEXECUTORCH_BUILD_EXECUTOR_RUNNER=OFF -DEXECUTORCH_BUILD_TESTS=OFF \
  -DEXECUTORCH_ENABLE_LOGGING=OFF \
  -DEXECUTORCH_BUILD_CORTEX_M=ON -DCMSIS_NN_LOCAL_PATH="$HOME/executorch/CMSIS-NN" \
  -DEXECUTORCH_SELECT_OPS_LIST="$OPS"
# Only the libraries we link (building "all" also tries XNNPACK, which fails bare-metal).
cmake --build "$B" -j"$(nproc)" --target executorch_core executorch executorch_selected_kernels \
  portable_kernels extension_data_loader cortex_m_ops_lib cortex_m_kernels cmsis-nn
echo "ExecuTorch libraries in $B"
