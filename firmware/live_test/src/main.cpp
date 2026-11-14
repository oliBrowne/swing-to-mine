/* Swing to Mine: live test. MPU-6050 at 100 Hz -> last 1 s of samples ->
 * int8 gesture model every 50 ms -> simple decoder -> LEDs.
 *
 *   green  = sword          (flash after a detection)
 *   orange = mine           (on while mining keeps winning)
 *   red    = place          (flash)
 *   blue   = swap left/right (flash)
 *   all off = nothing
 *
 * No UART or USB: everything worth seeing is in g_* variables that
 * firmware/live_test/scripts/watch.py reads over the ST-LINK while it runs.
 * If the sensor doesn't answer, the red LED stays on and g_imu_ok = 0.
 */
#include <cmath>
#include <cstdint>
#include <cstring>

#include "executorch/extension/data_loader/buffer_data_loader.h"
#include "executorch/runtime/core/exec_aten/exec_aten.h"
#include "executorch/runtime/core/hierarchical_allocator.h"
#include "executorch/runtime/core/portable_type/tensor_impl.h"
#include "executorch/runtime/executor/method.h"
#include "executorch/runtime/executor/program.h"
#include "executorch/runtime/platform/runtime.h"

#include "board.h"
#include "imu.h"
#include "model_pte.h"

using namespace executorch::runtime;
using namespace executorch::extension;
using namespace executorch::runtime::etensor;

/* ---- model input: same numbers as training/dataset.py ---- */
#define WINDOW 100                                  /* samples = 1 s at 100 Hz */
#define STRIDE 5                                    /* run the model every 5 samples = 50 ms */
static const float kAccelScale = 1.0f / 2048.0f;    /* counts -> g (+-16 g) */
static const float kGyroScale = 1.0f / (16.4f * 250.0f);  /* counts -> deg/s, then /250 */

/* ---- classes: training/labels.py order ---- */
enum { NOTHING, SWORD, MINE, PLACE, SWAP_LEFT, SWAP_RIGHT, N_CLASSES };

/* ---- decoder settings: keep in step with training/evaluate_stream.py ---- */
static const float kMinProb = 0.60f;       /* winner's softmax probability must be at least this */
static const int kAgreeRuns = 3;           /* ...for this many runs in a row (3 x 50 ms) */
static const uint32_t kCooldownMs = 300;   /* after a gesture fires, ignore new ones this long */
static const int kRearmRuns = 3;           /* a fired gesture must stop winning this many runs before it can fire again */
static const uint32_t kMineReleaseMs = 400;/* mine lets go after this long without being the top pick */
static const uint32_t kFlashMs = 300;      /* how long a one-shot LED stays on */

/* LED bits: 0 green, 1 orange, 2 red, 3 blue */
static const uint8_t kLedFor[N_CLASSES] = {0, 1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 3};

/* ---- ExecuTorch memory (int8 model: 3,008 B activations, ~4.7 KB method) ---- */
static uint8_t method_pool[16 * 1024] __attribute__((aligned(16)));
static uint8_t planned_pool[12 * 1024] __attribute__((aligned(16)));
static uint8_t temp_pool[8 * 1024] __attribute__((aligned(16)));
static float input_buf[WINDOW * 6] __attribute__((aligned(16)));   /* [time][channel] */
static int16_t ring[WINDOW][6];                                    /* raw counts, circular */

/* ---- read these over the ST-LINK (scripts/watch.py) ---- */
extern "C" {
volatile uint32_t g_magic = 0;              /* 0x11FE7E57 once running */
volatile uint32_t g_stage = 0;              /* 1 load, 2 method, 3 running, 4 execute failed */
volatile uint32_t g_error = 0;              /* ExecuTorch error code */
volatile uint32_t g_imu_ok = 0;
volatile uint32_t g_who_am_i = 0xFFFFFFFFu; /* 0x68 genuine MPU-6050, 0xFF = no answer */
volatile uint32_t g_imu_errors = 0;
volatile uint32_t g_samples = 0;
volatile int32_t g_last_sample[6];          /* raw counts */
volatile uint32_t g_inferences = 0;
volatile int32_t g_last_class = -1;         /* model's top class on the last run */
volatile float g_last_scores[N_CLASSES];    /* model output (logits) */
volatile float g_last_prob = 0.0f;          /* softmax probability of the top class */
volatile uint32_t g_events = 0;             /* decoder detections (not counting nothing) */
volatile uint32_t g_event_count[N_CLASSES]; /* per class; MINE counts mining starts */
volatile int32_t g_last_event = -1;
volatile uint32_t g_last_event_ms = 0;
volatile uint32_t g_infer_cycles_max = 0;   /* worst model run */
volatile uint32_t g_loop_cycles_max = 0;    /* worst sample->decision step incl. the model */
volatile uint32_t g_sample_gap_ms_max = 0;  /* worst gap between samples (should be 10) */
volatile uint32_t g_now_ms = 0;
}

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xE0001FB0u)

/* ExecuTorch is built without logging; keep its log hook silent. */
void et_pal_emit_log_message(et_timestamp_t, et_pal_log_level_t, const char *, const char *,
                             size_t, const char *, size_t) {}

static void fail(uint32_t stage, Error e) {
    g_stage = stage;
    g_error = (uint32_t)e;
    while (1) {
        led_set(0xF);
        board_delay_ms(100);
        led_set(0);
        board_delay_ms(100);
    }
}

/* Decoder state */
static int streak_class = -1, streak = 0;
static bool mining = false;
static uint32_t cooldown_until = 0, flash_until = 0, mine_last = 0;
static int blocked = -1, lost = 0;
static uint8_t flash_led = 0;

static void count_event(int cls, uint32_t now) {
    g_event_count[cls] = g_event_count[cls] + 1;
    g_events = g_events + 1;
    g_last_event = cls;
    g_last_event_ms = now;
}

/* Same rules as decode() in training/evaluate_stream.py:
 *  - a gesture fires after kAgreeRuns confident wins in a row, outside the cooldown
 *  - after firing it is blocked until the model has named something else
 *    kRearmRuns runs in a row (one swing stays in the 1 s window for a while)
 *  - mine is a hold: released once mine has not been the top pick for kMineReleaseMs */
static void decode(int raw, float prob, uint32_t now) {
    int top = (prob >= kMinProb) ? raw : NOTHING;
    streak = (top == streak_class) ? streak + 1 : 1;
    streak_class = top;

    if (blocked >= 0) {
        lost = (raw != blocked) ? lost + 1 : 0;
        if (lost >= kRearmRuns) blocked = -1;
    }
    if (mining) {
        if (raw == MINE) {
            mine_last = now;
        } else if (now - mine_last > kMineReleaseMs) {
            mining = false;
            cooldown_until = now + kCooldownMs;
        }
    } else if (top != NOTHING && streak >= kAgreeRuns &&
               (int32_t)(now - cooldown_until) >= 0 && top != blocked) {
        count_event(top, now);
        if (top == MINE) {
            mining = true;
            mine_last = now;
        } else {
            cooldown_until = now + kCooldownMs;
            blocked = top;
            lost = 0;
            flash_led = kLedFor[top];
            flash_until = now + kFlashMs;
        }
    }
    uint8_t leds = mining ? kLedFor[MINE] : 0;
    if ((int32_t)(now - flash_until) < 0) leds |= flash_led;
    led_set(leds);
}

int main(void) {
    board_init();
    DEMCR |= (1u << 24);
    DWT_LAR = 0xC5ACCE55u;
    DWT_CTRL |= 1u;

    /* Model */
    runtime_init();
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
    /* input [1, 6, 1, 100], channels-last = [time][channel] in memory */
    static TensorImpl::SizesType sizes[4] = {1, 6, 1, WINDOW};
    static TensorImpl::DimOrderType dim_order[4] = {0, 2, 3, 1};
    static TensorImpl::StridesType strides[4] = {6 * WINDOW, 1, 6 * WINDOW, 6};
    TensorImpl input_impl(ScalarType::Float, 4, sizes, input_buf, dim_order, strides);
    Tensor input_tensor(&input_impl);

    /* Sensor: red LED while it doesn't answer, retry once a second */
    uint8_t who = 0xFF;
    led_set(1u << 2);
    while (imu_init(&who) != 0) {
        g_who_am_i = who;
        g_imu_errors = g_imu_errors + 1;
        board_delay_ms(1000);
    }
    g_who_am_i = who;
    g_imu_ok = 1;
    led_set(0);

    g_stage = 3;
    g_magic = 0x11FE7E57u;
    uint32_t head = 0, filled = 0, since_run = 0;
    uint32_t last_sample_ms = board_ms();
    int16_t s[6];

    while (1) {
        uint32_t now = board_ms();
        g_now_ms = now;
        /* wait for the sensor's data-ready flag; fall back to 10 ms timing on clones without it */
        if (!imu_data_ready() && (now - last_sample_ms) < 11) continue;
        const uint32_t c0 = DWT_CYCCNT;
        if (imu_read(s) != 0) {
            g_imu_errors = g_imu_errors + 1;
            led_set(1u << 2);
            continue;
        }
        uint32_t gap = now - last_sample_ms;
        if (filled > 0 && gap > g_sample_gap_ms_max) g_sample_gap_ms_max = gap;
        last_sample_ms = now;
        memcpy(ring[head], s, sizeof(s));
        for (int i = 0; i < 6; ++i) g_last_sample[i] = s[i];
        head = (head + 1) % WINDOW;
        if (filled < WINDOW) filled++;
        g_samples = g_samples + 1;

        if (filled < WINDOW || ++since_run < STRIDE) continue;
        since_run = 0;

        /* oldest sample first, scaled like training/dataset.py */
        for (int t = 0; t < WINDOW; ++t) {
            const int16_t *r = ring[(head + t) % WINDOW];
            float *o = &input_buf[t * 6];
            for (int c = 0; c < 3; ++c) o[c] = r[c] * kAccelScale;
            for (int c = 3; c < 6; ++c) o[c] = r[c] * kGyroScale;
        }
        const uint32_t m0 = DWT_CYCCNT;
        Error e = method.set_input(EValue(input_tensor), 0);
        if (e == Error::Ok) e = method.execute();
        const uint32_t m1 = DWT_CYCCNT;
        if (e != Error::Ok) fail(4, e);
        if (m1 - m0 > g_infer_cycles_max) g_infer_cycles_max = m1 - m0;

        const float *out = method.get_output(0).toTensor().const_data_ptr<float>();
        int top = 0;
        for (int c = 0; c < N_CLASSES; ++c) {
            g_last_scores[c] = out[c];
            if (out[c] > out[top]) top = c;
        }
        float sum = 0.0f;
        for (int c = 0; c < N_CLASSES; ++c) sum += expf(out[c] - out[top]);
        const float prob = 1.0f / sum;
        g_last_class = top;
        g_last_prob = prob;
        g_inferences = g_inferences + 1;
        decode(top, prob, now);

        const uint32_t loop = DWT_CYCCNT - c0;
        if (loop > g_loop_cycles_max) g_loop_cycles_max = loop;
    }
}
