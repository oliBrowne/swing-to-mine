/* SWING_MODEL=ON: the int8 gesture model with ExecuTorch, as in
 * firmware/live_test/src/main.cpp (same memory sizes, same input scaling as
 * training/dataset.py). The model is embedded from training/out/model_test/model.pte. */
#include <cmath>
#include <cstdint>
#include <new>

#include "executorch/extension/data_loader/buffer_data_loader.h"
#include "executorch/runtime/core/exec_aten/exec_aten.h"
#include "executorch/runtime/core/hierarchical_allocator.h"
#include "executorch/runtime/core/portable_type/tensor_impl.h"
#include "executorch/runtime/executor/method.h"
#include "executorch/runtime/executor/program.h"
#include "executorch/runtime/platform/runtime.h"

#include "gesture_decoder.h"
#include "model_pte.h"
#include "model_runner.h"

using namespace executorch::runtime;
using namespace executorch::extension;
using namespace executorch::runtime::etensor;

static const float kAccelScale = 1.0f / 2048.0f;          /* counts -> g (+-16 g) */
static const float kGyroScale = 1.0f / (16.4f * 250.0f);  /* counts -> deg/s, then /250 */

static uint8_t method_pool[16 * 1024] __attribute__((aligned(16)));
static uint8_t planned_pool[12 * 1024] __attribute__((aligned(16)));
static uint8_t temp_pool[8 * 1024] __attribute__((aligned(16)));
static float input_buf[MODEL_WINDOW * 6] __attribute__((aligned(16)));   /* [time][channel] */

/* Objects that must live as long as the firmware runs; built in place by
 * model_init() (no heap). */
alignas(BufferDataLoader) static uint8_t loader_mem[sizeof(BufferDataLoader)];
alignas(Program) static uint8_t program_mem[sizeof(Program)];
alignas(MemoryAllocator) static uint8_t method_alloc_mem[sizeof(MemoryAllocator)];
alignas(MemoryAllocator) static uint8_t temp_alloc_mem[sizeof(MemoryAllocator)];
alignas(HierarchicalAllocator) static uint8_t planned_alloc_mem[sizeof(HierarchicalAllocator)];
alignas(MemoryManager) static uint8_t manager_mem[sizeof(MemoryManager)];
alignas(Method) static uint8_t method_mem[sizeof(Method)];
alignas(TensorImpl) static uint8_t input_impl_mem[sizeof(TensorImpl)];
static Method *method = nullptr;
static TensorImpl *input_impl = nullptr;
static Span<uint8_t> planned_span(planned_pool, sizeof(planned_pool));
extern "C" volatile uint32_t g_model_errors;   /* main.c: failed runs */

/* ExecuTorch is built without logging; keep its log hook silent. */
void et_pal_emit_log_message(et_timestamp_t, et_pal_log_level_t, const char *, const char *,
                             size_t, const char *, size_t) {}

extern "C" uint32_t model_init(void)
{
    runtime_init();
    auto *loader = new (loader_mem) BufferDataLoader(model_pte, model_pte_len);
    Result<Program> program = Program::load(loader, Program::Verification::Minimal);
    if (!program.ok()) return 1u * 256u + (uint32_t)program.error();
    auto *prog = new (program_mem) Program(std::move(program.get()));

    auto *method_alloc = new (method_alloc_mem) MemoryAllocator(sizeof(method_pool), method_pool);
    auto *planned = new (planned_alloc_mem) HierarchicalAllocator({&planned_span, 1});
    auto *temp_alloc = new (temp_alloc_mem) MemoryAllocator(sizeof(temp_pool), temp_pool);
    auto *manager = new (manager_mem) MemoryManager(method_alloc, planned, temp_alloc);
    Result<Method> m = prog->load_method("forward", manager);
    if (!m.ok()) return 2u * 256u + (uint32_t)m.error();
    method = new (method_mem) Method(std::move(m.get()));

    /* input [1, 6, 1, 100], channels-last = [time][channel] in memory */
    static TensorImpl::SizesType sizes[4] = {1, 6, 1, MODEL_WINDOW};
    static TensorImpl::DimOrderType dim_order[4] = {0, 2, 3, 1};
    static TensorImpl::StridesType strides[4] = {6 * MODEL_WINDOW, 1, 6 * MODEL_WINDOW, 6};
    input_impl = new (input_impl_mem) TensorImpl(ScalarType::Float, 4, sizes, input_buf, dim_order, strides);
    return 0;
}

extern "C" int model_available(void) { return method != nullptr; }

extern "C" int model_run(const int16_t window[MODEL_WINDOW][6], float *prob)
{
    *prob = 0.0f;
    if (!method) return CLS_NOTHING;
    for (int t = 0; t < MODEL_WINDOW; ++t) {
        float *o = &input_buf[t * 6];
        for (int c = 0; c < 3; ++c) o[c] = window[t][c] * kAccelScale;
        for (int c = 3; c < 6; ++c) o[c] = window[t][c] * kGyroScale;
    }
    Tensor input(input_impl);
    Error e = method->set_input(EValue(input), 0);
    if (e == Error::Ok) e = method->execute();
    if (e != Error::Ok) {
        g_model_errors = g_model_errors + 1;
        return CLS_NOTHING;
    }

    const float *out = method->get_output(0).toTensor().const_data_ptr<float>();
    int top = 0;
    for (int c = 1; c < N_CLASSES; ++c)
        if (out[c] > out[top]) top = c;
    float sum = 0.0f;
    for (int c = 0; c < N_CLASSES; ++c) sum += expf(out[c] - out[top]);
    *prob = 1.0f / sum;
    return top;
}
