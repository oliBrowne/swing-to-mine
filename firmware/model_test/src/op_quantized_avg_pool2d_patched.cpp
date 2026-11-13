/*
 * Patched copy of executorch/backends/cortex_m/ops/op_quantized_avg_pool2d.cpp
 * (ExecuTorch 1.3.0, Copyright 2025-2026 Arm Limited, BSD-style license).
 *
 * The upstream op passes a NULL scratch buffer to arm_avgpool_s8. That works
 * on MVE cores (buffer size 0) but on the Cortex-M4 (DSP path) CMSIS-NN needs
 * output_w * channels int32s and returns ARM_CMSIS_NN_ARG_ERROR. This copy
 * takes the buffer from the temp allocator, like op_quantized_conv2d does.
 * Linked into the firmware, it replaces the library version of the symbol.
 */
#include "cortex_m_ops_common.h"

namespace cortex_m {
namespace native {

using KernelRuntimeContext = torch::executor::KernelRuntimeContext;

Tensor& quantized_avg_pool2d_out(
    KernelRuntimeContext& context,
    const Tensor& input,
    const Int64ArrayRef kernel_size,
    const Int64ArrayRef stride,
    const Int64ArrayRef padding,
    const int64_t zero_point,
    const int64_t multiplier,
    const int64_t shift,
    Tensor& out) {
  constexpr int32_t activation_min = std::numeric_limits<int8_t>::min();
  constexpr int32_t activation_max = std::numeric_limits<int8_t>::max();

  const int64_t dilation_values[2] = {1, 1};
  const Int64ArrayRef dilation(dilation_values, 2);
  CmsisPool2DConfig pool_config;
  if (!prepare_cmsis_pool2d_config(
          context, "quantized_avg_pool2d_out", input, out, kernel_size, stride,
          padding, dilation, false, activation_min, activation_max, pool_config)) {
    return out;
  }

  cmsis_nn_context cmsis_ctx;
  cmsis_ctx.buf = nullptr;
  cmsis_ctx.size = 0;
  const int32_t buffer_bytes = arm_avgpool_s8_get_buffer_size(
      pool_config.output_dims.w, pool_config.input_dims.c);
  if (buffer_bytes > 0) {
    auto buffer_or_error = context.allocate_temp(buffer_bytes, 16);
    if (!buffer_or_error.ok()) {
      ET_LOG(Error, "quantized_avg_pool2d_out: no temp memory for %d bytes", (int)buffer_bytes);
      context.fail(buffer_or_error.error());
      return out;
    }
    cmsis_ctx.buf = buffer_or_error.get();
    cmsis_ctx.size = buffer_bytes;
  }

  const arm_cmsis_nn_status status = arm_avgpool_s8(
      &cmsis_ctx, &pool_config.pool_params, &pool_config.input_dims,
      input.const_data_ptr<int8_t>(), &pool_config.filter_dims,
      &pool_config.output_dims, out.mutable_data_ptr<int8_t>());
  if (status != ARM_CMSIS_NN_SUCCESS) {
    ET_LOG(Error, "quantized_avg_pool2d_out: arm_avgpool_s8 failed with status [%d]", status);
    context.fail(Error::Internal);
  }

  (void)zero_point;
  (void)multiplier;
  (void)shift;
  return out;
}

} // namespace native
} // namespace cortex_m
