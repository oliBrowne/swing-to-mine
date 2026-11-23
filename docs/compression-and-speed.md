# Shrinking and speeding up the gesture model (Labs 3 and 4)

This page applies the ideas of **Lab 3** (channel pruning, fine-tuning, int8 with ExecuTorch, simulating the chip) and **Lab 4** (where the compute time goes, and how the hardware runs each layer) to the Swing to Mine gesture model. Everything in sections 1 to 4 was run on Nov 23 **without the board**: on a PC and in the Renode simulator. Section 5 is the test plan for the parts that need the real board, with empty tables to fill in.

**Short version**

- The model is already tiny (6,774 parameters, 22 KB file, 3.41 ms per run on the board, measured Nov 13). It fits easily, so pruning is not *needed*; we did it to measure the trade-off.
- **Pruning works, but it is no better than training the smaller model directly.** At every size, a small model trained from scratch was as good as or better than a pruned-and-fine-tuned one. With the same amount of training after pruning, the gap closes.
- **The choice of which channels to cut barely matters here.** Cutting by batch-norm scale (the best method in Lab 3), by weight size (L1), or at random gave results within the noise of each other, except at the smallest size.
- **int8 costs nothing:** at every size the int8 model scores within 0.2 points of the float one (one exception: +1.1 for the smallest bn model) and gives the same answer on 98–100% of windows.
- **A 40% smaller model loses nothing:** the 75% width (12/18/24 channels, 4,002 parameters) trained from scratch scored 97.3% vs 97.0% for the full model. Below half width, accuracy and the replay hit rate start to drop.
- **Speed does not shrink as fast as size.** Cutting 89% of the multiply-adds removes only ~68% of the work, because about half of each convolution's work is moving data around, not multiplying. Estimated time per run: 3.41 ms (full, measured) → about 2.5 ms (75%) → 1.7 ms (50%) → 1.1 ms (25%). All of these are a small slice of the 50 ms between model runs.
- **Recommendation:** keep the current full model for the demo. If we want to show compression in the presentation, the 75% model is the honest example (smaller, same accuracy). Real board numbers for the smaller models are still to be measured (section 5).

## 1. How the tests were run

- **Data:** all recordings on `main` on Nov 23: 11,126 one-second windows from 8 people. P03 has too little data to be a test person and is only used for training, so there are **7 test people** (P01, P02, P05, P06, P07, P10, P11).
- **Leave one person out:** for each test person, train on everyone else and test on that person. Every number below is for people the model never saw, averaged over the 7 people.
- **Seeds:** 3 training seeds (0, 1, 2) for the main table. The full model's average moved between 96.4% and 97.6% from seed to seed, so **differences under about 1 point are noise**.
- **Window accuracy** (`float`, `int8`): % of 1 s windows given the right class.
- **Replay** (`replay`, `false/min`): the whole recording is played through the model every 50 ms with the same trigger rules the board uses (`training/evaluate_stream.py`). `replay` = % of cued gestures that fired the right action; `false/min` = gestures that fired while the person was idle or between cues. This is closer to real play than window accuracy.
- **int8:** post-training quantization with the board's `CortexMQuantizer` (the same code as `training/export.py`), calibrated on the training people only, scored with PyTorch's quantized reference on the PC. On the full model this matches the board closely (Nov 13: same top gesture on all test windows).
- Script: [`training/prune.py`](../training/prune.py). One run of 7 people × 17 variants takes ~25 minutes on 4 CPU cores. Raw results go to `training/out/prune_runs.jsonl` (not committed).

```bash
python training/prune.py --seeds 0 1 2                                   # the main table (needs the ExecuTorch env for int8)
python training/prune.py --seeds 0 --finetune-epochs 30 --log training/out/prune_runs_ft30.jsonl   # the fairness check
python training/prune.py --summary                                       # print the table again
```

## 2. Lab 3: pruning, fine-tuning and int8

### What pruning does here

The model has three convolution layers with 16, 24 and 32 channels (filters). Pruning removes the least useful channels from each layer, along with the weights that read from them in the next layer, so the model really gets smaller (not just zeros in the same shape). We keep the same fraction of channels in every layer:

| Keep | Channels | Parameters | Multiply-adds per run |
|---|---|---|---|
| 100% (current model) | 16 / 24 / 32 | 6,774 | 131,712 |
| 75% | 12 / 18 / 24 | 4,002 | 80,424 |
| 50% | 8 / 12 / 16 | 1,950 | 41,376 |
| 35% | 6 / 8 / 11 | 1,054 | 24,386 |
| 25% | 4 / 6 / 8 | 618 | 14,568 |

Ways of picking which channels to keep:

- **bn:** keep the channels whose batch-norm scale is largest. This was the best criterion in Lab 3.
- **l1:** keep the channels whose filter weights are largest (L1 norm).
- **random:** keep a random set. This is the control: if a criterion can't beat random, it isn't doing much.
- **scratch:** don't prune at all; train a model that is this small from the start (30 epochs). This is the honest baseline: pruning is only worth it if it beats this.

Each pruned model is fine-tuned for 10 epochs at a lower learning rate (the full model trains for 30).

### Results (3 seeds × 7 unseen people)

| Keep | Method | float | int8 | int8 same answer as float | Replay: right gesture fired | False triggers / min |
|---|---|---|---|---|---|---|
| 100% | full model | 97.0% | 97.0% | 99.8% | 94.7% | 0.10 |
| 75% | bn | 96.2% | 96.2% | 99.7% | 93.4% | 0.12 |
| 75% | l1 | 96.7% | 96.7% | 99.8% | 93.9% | 0.16 |
| 75% | random | 96.8% | 96.8% | 99.7% | 94.6% | 0.14 |
| 75% | **scratch** | **97.3%** | **97.4%** | 99.9% | **95.4%** | 0.16 |
| 50% | bn | 94.8% | 94.8% | 99.6% | 92.1% | 0.22 |
| 50% | l1 | 94.1% | 94.2% | 99.5% | 91.4% | 0.22 |
| 50% | random | 94.5% | 94.5% | 99.6% | 90.8% | 0.32 |
| 50% | **scratch** | **95.9%** | **95.7%** | 99.6% | **93.3%** | 0.21 |
| 35% | bn | 93.1% | 93.1% | 99.4% | 90.5% | 0.35 |
| 35% | l1 | 93.9% | 93.8% | 99.5% | 91.0% | 0.38 |
| 35% | random | 93.1% | 93.1% | 99.5% | 89.5% | 0.36 |
| 35% | **scratch** | **95.1%** | **95.1%** | 99.6% | **93.5%** | 0.37 |
| 25% | bn | 88.9% | 90.0% | 98.2% | 83.6% | 0.28 |
| 25% | l1 | 87.4% | 87.4% | 99.1% | 83.6% | 0.49 |
| 25% | random | 90.0% | 89.9% | 99.1% | 80.6% | 0.39 |
| 25% | **scratch** | **94.2%** | **94.2%** | 99.4% | **91.4%** | 0.44 |

The weakest gesture was `place` for almost every variant (85–94%), and `mine` for some of the smallest ones. Per seed, the full model averaged 96.4 / 97.6 / 97.1% and the 50% scratch model 95.1 / 96.3 / 96.2%.

Per person, the full model vs the 50% scratch model vs the 50% bn-pruned model (float, 3 seeds): P01 99.5 / 99.3 / 98.9, P02 99.4 / 98.1 / 98.7, P05 97.3 / 95.8 / 94.0, P06 97.9 / 96.4 / 94.4, P07 93.1 / 92.6 / 91.1, P10 96.4 / 95.3 / 93.0, P11 95.8 / 93.5 / 93.6. The people the model finds hardest (P07, P10, P11) lose the most when the model shrinks.

### Is pruning only behind because it trains less? (fairness check, seed 0)

`scratch` trains for 30 epochs; the pruned models get 30 epochs before pruning plus 10 after. To check whether the pruned models just needed more training after the cut, seed 0 was run again with **30 epochs of fine-tuning** (same 7 people):

| Keep | bn | l1 | random | scratch |
|---|---|---|---|---|
| 75% | 95.1% (was 95.5) | 96.7% (96.6) | 96.8% (96.1) | 97.4% |
| 50% | 94.5% (93.6) | 94.5% (92.7) | 95.2% (94.0) | 95.1% |
| 35% | 94.4% (92.7) | 95.2% (93.4) | 95.3% (93.4) | 94.2% |
| 25% | 92.7% (85.6) | 89.8% (85.3) | 92.1% (90.1) | 93.4% |

(float window accuracy; in brackets the same seed with 10 epochs; full model on this seed: 96.4%.) With the longer fine-tune, pruned models catch up with training from scratch at 50% and 35% and get close at 25%. So the gap in the main table is mostly a training-budget effect: **pruning ends up about as good as training small, not better.**

### What this means (and how it compares to Lab 3)

- In Lab 3 the CIFAR-10 network was much bigger and pruning was the only way to fit the flash; batch-norm scale came out as the best criterion there (L1 ≈ L2, APoZ worst). Even after pruning, that model file was ~137 KB; this whole gesture model is 22 KB, there's little redundancy to find, and the firmware already fits in about 11% of the chip's flash. When there is nothing big to remove, which channels you remove matters little and fine-tuning does most of the work.
- **int8 quantization is free at every size**, as in Lab 3. The only visible int8 effect is at 25% width, where the model has so few channels that rounding matters more (the simulated chip's scores differ more from the PC's, see section 3).
- The real limit on this project is **data, not model size**: the weak spots (slow swings, `place` while looking around) are the same for every size, and they are where the smallest models lose accuracy first.

## 3. Size on the chip and simulated speed (Renode)

Each model was exported to int8 with `training/export.py`, built into [`firmware/model_test`](../firmware/model_test/) (the same bench that measured 3.41 ms on the board), and run in the **Renode 1.15.3** simulator of the STM32F4 Discovery. The simulated chip picks the right gesture on all 6 test windows for every size, with scores within 0.29 (full), 0.36 (75%), 0.47 (50%) and 0.92 (25%) of what the PC export expects.

**How the time estimate works, and its limits.** Renode runs the real firmware instruction by instruction, but it does not model how long each instruction takes on the real chip (flash wait states, pipeline stalls), and the cycle counter the firmware uses for timing reads 0 in Renode. So we count **instructions per inference** in Renode (`firmware/model_test/sim/renode_count.sh`). On the board, the full model took 573,250 cycles (3.41 ms, measured Nov 13); in Renode the identical build (same 116,912 B of flash) runs 400,000 instructions. That is **1.43 cycles per instruction**. The estimated time for the other sizes assumes the same ratio, which is reasonable because they run exactly the same code (the same CMSIS-NN functions) on smaller arrays. As a check, the average-pooling model from Nov 13 predicts 3.42 ms against 3.43 ms measured, but that is a very similar model, so the ratio is only proven for the full size. **The estimates for the smaller models are not measurements**; section 5 has the test to confirm them.

| Model | Model file (.pte) | Firmware flash (`text`+`data`) | RAM (`data`+`bss`) | Instructions per run (Renode) | Time per run | Share of the 50 ms between runs |
|---|---|---|---|---|---|---|
| medium (24/48/64), for comparison | 55,384 B | 150,480 B | 41,848 B | 923,076 | ~7.9 ms (est.) | ~16% |
| **full (16/24/32), current** | **21,816 B** | **116,912 B** | **41,848 B** | **400,000** | **3.41 ms (measured on the board)** | **6.8%** |
| 75% (12/18/24) | 15,928 B | 111,024 B | 41,848 B | 292,682 | ~2.50 ms (est.) | ~5.0% |
| 50% (8/12/16) | 11,320 B | 106,416 B | 41,848 B | 198,816 | ~1.70 ms (est.) | ~3.4% |
| 35% (6/8/11) | 9,432 B | 104,528 B | 41,848 B | 157,450 | ~1.34 ms (est.) | ~2.7% |
| 25% (4/6/8) | 8,312 B | 103,408 B | 41,848 B | 129,729 | ~1.11 ms (est.) | ~2.2% |

Notes:

- Flash and RAM are from `arm-none-eabi-size` on the cloud build (`arm-none-eabi-gcc` 13.2.1, ExecuTorch 1.3.1). The full model's numbers match the Nov 13 board build byte for byte. Flash is mostly the ExecuTorch runtime and kernels (~95 KB); the model itself is 8–22 KB of it. Out of the chip's 1 MB of flash, the firmware uses 10–11% at every pruned size (14% for medium).
- RAM is the same for all sizes because the memory pools in `model_test` are fixed in size; the model needs only 3,008 B of activations and 4,416 B of method memory at every size.
- The instruction counts don't depend on the trained weights: the same widths with different weights gave identical counts.

## 4. Lab 4: where the time goes

Lab 4 was about scheduling a CNN onto hardware (a simulated systolic array) and counting where the cycles go. Here the hardware is fixed, a Cortex-M4 with DSP instructions that CMSIS-NN uses, but the same questions apply: what does each step cost, and is it multiplying or moving data?

### Speed-ups measured on the board (Nov 13)

Same model, same board, each step measured with the cycle counter ([results.md](results.md#model-on-the-board-nov-13)):

| Step | Time per run | Speed-up |
|---|---|---|
| float32, ExecuTorch portable kernels, built for size (`-Os`, the Lab 3 setup) | 152.1 ms | 1× |
| float32, built for speed (`-O3`) | 64.5 ms | 2.4× |
| float32, `-O3`, batch norm folded into the convolutions | 64.0 ms | 2.4× |
| int8, CMSIS-NN kernels | 21.3–25.5 ms | ~6× |
| int8, CMSIS-NN, without `-mno-unaligned-access` | **3.43 ms** | **44×** |

The last step was found by sampling where the chip spent its time: ~70% was in `memcpy` calls that a single compiler flag had created inside CMSIS-NN's inner loop. Removing the flag gave the 6× jump. That is the Lab 4 lesson in miniature: moving data can cost more than the arithmetic.

### Profile of one inference (Renode, Nov 23)

Renode's profiler records which function every executed instruction belongs to. Share of instructions per function, int8 model:

| Where the instructions go | Full model (16/24/32) | 25% model (4/6/8) |
|---|---|---|
| Convolution: multiply-accumulate inner loop (`arm_nn_mat_mult_kernel_s8_s16`) | 45% | 23% |
| Convolution: gathering each output's input values into a column (`arm_convolve_s8`'s own code) | 43% | 47% |
| Copies inside the convolution and of the input (`memcpy`) | 3.5% | 9.6% |
| Widening int8 to int16 for the DSP multiply (`arm_s8_to_s16_unordered_with_offset`) | 2.3% | 4.1% |
| Turning the float input into int8 (`quantize_per_tensor` + `nearbyintf`) | 4.5% | 14% |
| Max pooling, final linear layer, ExecuTorch runtime | ~1.5% | ~3% |

What this shows:

- **The convolutions are ~94% of the work**, and only about half of that is actual multiplying. The rest is getting data into place for the multiply loop.
- **Why speed doesn't shrink as fast as the model:** the full model does ~3 instructions per multiply-add, the 25% model ~9. The data gathering and the input conversion (600 values every run, whatever the model size) don't get smaller as fast as the multiplies do. A straight-line fit over the five pruned sizes gives roughly **100,000 instructions of near-fixed cost + 2.3 instructions per multiply-add** (the medium model does a bit better than that line, ~2.1 per multiply-add).
- **What could still be made faster** (not done; the CPU budget doesn't need it): converting the input to int8 with integer math instead of `nearbyintf` (~4% for the full model), and running the model every 100 ms instead of 50 ms (halves the CPU use, but adds up to 50 ms of reaction time).

These percentages come from the simulator's instruction counts, not from real cycles. On the board, memory-heavy code tends to cost a bit more per instruction than arithmetic, so the data-movement share is probably, if anything, a little higher on the real chip.

Reproduce: `firmware/model_test/sim/renode_count.sh` (instructions per inference) and, for the profile, Renode's `sysbus.cpu EnableProfiler CollapsedStack @file true` on a `-DSIM_NO_DELAY` build.

## 5. Tests that need the board (to fill in)

Whoever has the board: run these in order and fill in the tables, then copy the filled tables into [results.md](results.md). Each test says what it needs. Tests A and B need the ExecuTorch setup from [`firmware/swing_to_mine/README.md`](../firmware/swing_to_mine/README.md#build) (Linux or WSL).

### Test A: speed of each model size on the real chip (~20 min)

Checks the simulator estimates in section 3. Prebuilt firmware for the four sizes exists in Oliver's project files (`final-project/pruning-speed-test/<size>/`, with the `.elf` and the expected scores); otherwise build them:

```bash
# once per size: KEEP = 1.0, 0.75, 0.5, 0.25  ->  NAME = full, bn75, bn50, bn25
python training/prune.py --save bn 0.5 --finetune-epochs 30          # writes training/out/gesture_bn50.pt
python training/export.py --checkpoint training/out/gesture_bn50.pt --out-dir training/out/model_test_bn50
cd firmware/model_test
cmake -B build-bn50 -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-gcc-toolchain.cmake -DMODEL_DIR=$PWD/../../training/out/model_test_bn50
cmake --build build-bn50
bash scripts/flash.sh build-bn50/model_test.bin
python scripts/readback.py build-bn50/model_test.elf ../../training/out/model_test_bn50
```

`readback.py` prints the time per run, the cycles, and whether the board picks the same gesture as the PC.

| Model | Estimate (section 3) | Measured ms per run | Cycles (min–max) | Flash (`arm-none-eabi-size`) | Same top gesture as PC (of 6) | Max score difference |
|---|---|---|---|---|---|---|
| full (16/24/32) | 3.41 ms (measured Nov 13) | | | | | |
| 75% (12/18/24) | ~2.50 ms | | | | | |
| 50% (8/12/16) | ~1.70 ms | | | | | |
| 25% (4/6/8) | ~1.11 ms | | | | | |

If a measured time is more than ~10% off its estimate, the cycles-per-instruction ratio isn't the same across sizes; note that next to the table.

### Test B: the whole controller loop with the model (~15 min per model)

How much of the 50 ms budget the controller really uses, sensor reading and USB included. Build [`firmware/swing_to_mine`](../firmware/swing_to_mine/) with `-DSWING_MODEL=ON -DMODEL_DIR=<the model_test_... folder>`, flash, play for 2 minutes, then read `g_loop_us_max` and `g_inferences` over the ST-LINK (as in that README).

| Model | Worst loop step `g_loop_us_max` | Inferences in 2 min (`g_inferences`, expect ~2,400) | Any restarts / errors (`g_model_error`) |
|---|---|---|---|
| full | | | |
| 50% | | | |

### Test C: live accuracy, full vs smaller model (~30 min)

The PC numbers say the 75% model is as good as the full one and the 50% model is ~1–2 points worse. This checks that with a person holding the board. Same person, same day, board flashed with each model in turn, look mode, in Notepad or a Minecraft test world. Call out 10 of each gesture in a random order (e.g. let `host/record.py`'s cue order guide you), count what fires (red LED blink + what appears on screen), then 2 minutes of just looking around and count anything that fires.

| Model | sword fired right (of 10) | mine (of 10) | place (of 10) | swap left (of 10) | swap right (of 10) | Wrong gesture fired | Accidental gestures in 2 min of looking around |
|---|---|---|---|---|---|---|---|
| full | | | | | | | |
| 75% | | | | | | | |
| 50% | | | | | | | |

### Test D: reaction time, motion to click (~20 min)

The target is under 100 ms from the motion to the click ([requirements.md](requirements.md)). Film the board and the screen together with a phone in slow motion (240 fps if possible), in Paint with the brush tool so a click leaves a dot. Do 10 sword swings. For each, count frames from the start of the swing to the dot appearing, ÷ fps. Expect the model's 3 confirmations (3 × 50 ms) to dominate; the model's own few ms barely matter, which is why a smaller model is not expected to change this number much.

| Swing | Frames | ms |
|---|---|---|
| 1 | | |
| 2 | | |
| 3 | | |
| 4 | | |
| 5 | | |
| 6 | | |
| 7 | | |
| 8 | | |
| 9 | | |
| 10 | | |
| **Median** | | |

Screen refresh adds up to one frame of the monitor (~17 ms at 60 Hz); note the monitor's refresh rate.

### Test E (optional): power

With a USB power meter between the PC and the board's USB port: current with the controller idle in look mode, and with the model on vs off (`SWING_MODEL=OFF` build). The model runs ~7% of the time, so a small difference is expected.

| Build | Current (mA) | Notes |
|---|---|---|
| no model | | |
| full model | | |
| 50% model | | |

## Files

| File | What it is |
|---|---|
| [`training/prune.py`](../training/prune.py) | Pruning test (bn / l1 / random / scratch, float + int8 + replay) and `--save` for a pruned everyone-model |
| [`training/model.py`](../training/model.py) | `GestureNet` now also takes three channel counts; `count_macs()` |
| [`training/export.py`](../training/export.py) | `--checkpoint` and `--out-dir` to export any model |
| [`firmware/model_test/sim/renode_count.sh`](../firmware/model_test/sim/renode_count.sh) | Instructions per inference in Renode, plus the simulated chip's answers |
