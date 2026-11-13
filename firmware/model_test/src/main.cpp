/* Swing to Mine: model speed test on the STM32F4 Discovery.
 *
 * Runs the exported gesture model (.pte from training/export.py) on one real
 * recorded window per class, over and over, and leaves the results in g_*
 * variables that OpenOCD reads through the ST-LINK (scripts/readback.py), so
 * no UART is needed. Green LED toggles per inference; red blinks on an
 * ExecuTorch error (stage in g_stage, code in g_error). Clock: 168 MHz.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "executorch/extension/data_loader/buffer_data_loader.h"
#include "executorch/runtime/core/exec_aten/exec_aten.h"
#include "executorch/runtime/core/hierarchical_allocator.h"
#include "executorch/runtime/core/portable_type/tensor_impl.h"
#include "executorch/runtime/executor/method.h"
#include "executorch/runtime/executor/program.h"
#include "executorch/runtime/platform/runtime.h"

#include "board.h"
#include "model_pte.h"      /* the .pte as a C array, generated at configure time */
#include "test_windows.h"   /* kTestWindows, kRefScores (written by training/export.py) */

using namespace executorch::runtime;
using namespace executorch::extension;
using namespace executorch::runtime::etensor;

/* Memory for ExecuTorch. Sizes are generous: the int8 model needs 3,008 B of
 * activations and ~4.7 KB of method memory (measured, see docs/results.md). */
#define PLANNED_POOL_BYTES (12 * 1024)    /* activations (export prints non_const_buffer_sizes) */
#define METHOD_POOL_BYTES  (16 * 1024)    /* method/runtime bookkeeping */
#define TEMP_POOL_BYTES    (8 * 1024)     /* CMSIS-NN scratch (im2col etc.), reset every run */
static uint8_t method_pool[METHOD_POOL_BYTES] __attribute__((aligned(16)));
static uint8_t planned_pool[PLANNED_POOL_BYTES] __attribute__((aligned(16)));
static uint8_t temp_pool[TEMP_POOL_BYTES] __attribute__((aligned(16)));
static float input_buf[6 * 100] __attribute__((aligned(16)));

/* ---- results, read with OpenOCD (scripts/readback.py) ---- */
extern "C" {
volatile uint32_t g_magic = 0;                  /* 0x600D5EED once running */
volatile uint32_t g_stage = 0;                  /* 1 load, 2 method, 3 running, 4 execute failed, 5 set_input failed */
volatile uint32_t g_error = 0;                  /* ExecuTorch error code, 0 = ok */
volatile uint32_t g_iterations = 0;
volatile uint32_t g_cycles_last[N_TESTS];       /* DWT cycles of the last run, per window */
volatile uint32_t g_cycles_min = 0xFFFFFFFFu;
volatile uint32_t g_cycles_max = 0;
volatile uint32_t g_method_pool_used = 0;       /* bytes */
volatile uint32_t g_pte_bytes = 0;
volatile float g_scores[N_TESTS][N_CLASSES];    /* board output per test window */
volatile int32_t g_argmax[N_TESTS];
volatile float g_max_abs_diff = 0.0f;           /* vs kRefScores, over all windows */
volatile char g_log[1024];                      /* ExecuTorch log (only if built with logging) */
volatile uint32_t g_log_len = 0;
}

/* DWT cycle counter (Cortex-M4 debug registers) */
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xE0001FB0u)

/* ExecuTorch log messages go into g_log instead of a console. */
void et_pal_emit_log_message(et_timestamp_t, et_pal_log_level_t level, const char *filename,
                             const char *, size_t line, const char *message, size_t length) {
    char head[48];
    const char *base = filename;
    for (const char *p = filename; p && *p; ++p)
        if (*p == '/') base = p + 1;
    int n = snprintf(head, sizeof(head), "%c %s:%u ", (char)level, base ? base : "?", (unsigned)line);
    for (int i = 0; i < n && g_log_len < sizeof(g_log) - 1; ++i) g_log[g_log_len++] = head[i];
    for (size_t i = 0; i < length && g_log_len < sizeof(g_log) - 1; ++i) g_log[g_log_len++] = message[i];
    if (g_log_len < sizeof(g_log) - 1) g_log[g_log_len++] = '\n';
}

static void fail(uint32_t stage, Error e) {
    g_stage = stage;
    g_error = (uint32_t)e;
    while (1) {
        led_set(1u << 2);
        board_delay_ms(200);
        led_set(0);
        board_delay_ms(200);
    }
}

int main(void) {
    board_init();
    DEMCR |= (1u << 24);   /* TRCENA */
    DWT_LAR = 0xC5ACCE55u;
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;        /* CYCCNTENA */

    runtime_init();
    g_pte_bytes = model_pte_len;
    g_stage = 1;
    BufferDataLoader loader(model_pte, model_pte_len);
    Result<Program> program_result = Program::load(&loader, Program::Verification::Minimal);
    if (!program_result.ok()) fail(1, program_result.error());
    Program program = std::move(program_result.get());

    g_stage = 2;
    MemoryAllocator method_allocator(sizeof(method_pool), method_pool);
    Span<uint8_t> planned_span(planned_pool, sizeof(planned_pool));
    HierarchicalAllocator planned_allocator({&planned_span, 1});
    MemoryAllocator temp_allocator(sizeof(temp_pool), temp_pool);
    MemoryManager memory_manager(&method_allocator, &planned_allocator, &temp_allocator);
    Result<Method> method_result = program.load_method("forward", &memory_manager);
    if (!method_result.ok()) fail(2, method_result.error());
    Method method = std::move(method_result.get());
    /* bump allocator: the next 1-byte allocation sits where the used part ends */
    g_method_pool_used = (uint32_t)((uint8_t *)method_allocator.allocate(1, 1) - method_pool);

#ifdef INPUT_CHANNELS_LAST
    /* int8 model: [1, 6, 1, 100] in channels-last (NHWC) memory order */
    static TensorImpl::SizesType sizes[4] = {1, 6, 1, 100};
    static TensorImpl::DimOrderType dim_order[4] = {0, 2, 3, 1};
    static TensorImpl::StridesType strides[4] = {600, 1, 600, 6};
    TensorImpl input_impl(ScalarType::Float, 4, sizes, input_buf, dim_order, strides);
#else
    /* float model: [1, 6, 100] */
    static TensorImpl::SizesType sizes[3] = {1, 6, 100};
    static TensorImpl::DimOrderType dim_order[3] = {0, 1, 2};
    static TensorImpl::StridesType strides[3] = {600, 100, 1};
    TensorImpl input_impl(ScalarType::Float, 3, sizes, input_buf, dim_order, strides);
#endif
    Tensor input_tensor(&input_impl);

    g_stage = 3;
    g_magic = 0x600D5EEDu;
    uint8_t led = 0;
    while (1) {
        float maxdiff = 0.0f;
        for (int w = 0; w < N_TESTS; ++w) {
            memcpy(input_buf, kTestWindows[w], sizeof(input_buf));
            const uint32_t c0 = DWT_CYCCNT;
            Error e = method.set_input(EValue(input_tensor), 0);   /* re-bind every run */
            if (e != Error::Ok) fail(5, e);
            e = method.execute();
            const uint32_t cycles = DWT_CYCCNT - c0;
            if (e != Error::Ok) fail(4, e);

            const float *out = method.get_output(0).toTensor().const_data_ptr<float>();
            int best = 0;
            for (int c = 0; c < N_CLASSES; ++c) {
                g_scores[w][c] = out[c];
                float d = out[c] - kRefScores[w][c];
                if (d < 0) d = -d;
                if (d > maxdiff) maxdiff = d;
                if (out[c] > out[best]) best = c;
            }
            g_argmax[w] = best;
            g_cycles_last[w] = cycles;
            if (cycles < g_cycles_min) g_cycles_min = cycles;
            if (cycles > g_cycles_max) g_cycles_max = cycles;
            g_iterations = g_iterations + 1;
            led ^= 1u;  /* green toggles per inference */
            led_set(led);
        }
        g_max_abs_diff = maxdiff;
        board_delay_ms(50);
    }
}
