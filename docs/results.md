# Results

Measured numbers only, each with how it was measured. Add new results at the top of their section.

## Max pooling instead of average (Nov 13, afternoon)

`training/model.py` now pools the last 13 time steps with **max** instead of the average (`--pool`, default `max`). Retrained with `train.py` (seed 0), exported with `export.py`, run on the board with `firmware/model_test`, measured the same way as the table below:

| Model | ms per inference | Flash | RAM | Output vs PC |
|---|---|---|---|---|
| int8, max pooling (CMSIS-NN `quantized_max_pool2d`) | **3.41** (573.0k–573.5k cycles) | 116,912 B | 41,848 B | max diff 0.29 vs float, top gesture 6/6 |

The model file is 21,816 B. It needs 3,008 B of activations and 4,416 B of method memory. Leave one person out, float vs int8 (Python reference ops): **P01 97.0% / 96.9%**, **P02 94.7% / 94.6%**, same answer on 99.9% and 99.4% of windows. The weakest class is swap_right on P02 (84% float, 83% int8). All 5,426 windows with the everyone-model: int8 and float give the same top class on 100.00%.

### Architecture sweep behind the change

Leave one person out (P01, P02 as test people), the same training as `train.py` (30 epochs, augmentation on), **3 seeds per variant**, % of 1 s windows right on the unseen person, mean of the 6 runs. Seed-to-seed spread is about **±0.5 points**, so differences under ~0.7 are noise. Only 2 test people and cut-out gestures: treat this as a direction, not a final number. All variants use only layers the board's int8 path runs. Float, on the PC.

| Variant | Accuracy | Params | Multiply-adds |
|---|---|---|---|
| small, average pooling (the first model) | 95.5% | 6,774 | 132k |
| **small, max pooling (now the default)** | **96.3%** | 6,774 | 132k |
| small, average + max pooling side by side | 96.6% | 6,966 | 132k |
| small, avg + max, 60 epochs | 96.7% | 6,966 | 132k |
| medium widths (24/48/64), average | 96.1% | 22,790 | 394k |
| medium, avg + max | 96.6% | 23,174 | 395k |
| bigger kernels 9/7/7 | 95.7% | 9,270 | 180k |
| 4th conv layer | 95.7% | 11,574 | 192k |
| residual blocks / dilated residual | 95.6% / 95.8% | 12,486 | 253k |
| dropout 0.3 before the head | 95.3% | 6,774 | 132k |
| 60 epochs (average pooling) | 95.7% | 6,774 | 132k |
| tiny widths (8/16/16) | 94.9% | 2,438 | 50k |
| depthwise-separable + residual | 94.4% (one seed 91.6) | 10,574 | 151k |
| no augmentation | 94.6% | 6,774 | 132k |

Max was picked over avg + max because the two are within noise, and max needs no concat op on the board. The weakest class stayed sword or swap_right (~89–91% recall) for every architecture, which points at data more than model size.

## Model on the board (Nov 13)

(These rows are the first model, with average pooling.)

Setup: STM32F4 Discovery (STM32F407VG) at **168 MHz** (8 MHz crystal → PLL; checked by reading the PLL/clock registers, and the cycle counter advanced 336.0 M cycles in 2,000 SysTick ms). Model: `small` GestureNet (6,774 parameters) trained on P01 + P02 + P03 by `training/train.py`. Firmware: [`firmware/model_test`](../firmware/model_test/), ExecuTorch 1.3.0, `arm-none-eabi-gcc` 13.2.1.

Time = DWT cycle counter around `set_input` + `execute` for one window, ÷ 168 MHz. Each run loops over one real recorded window per gesture (the middle window of each class from `training/dataset.py`); all 6 windows took the same time to within 0.1%. Results were read from RAM over the ST-LINK with OpenOCD. Flash = `text + data` and RAM = `data + bss` from `arm-none-eabi-size`. RAM includes the ExecuTorch memory pools (sized generously, see below), not the stack.

| Version | ms per inference | Flash | RAM | Output vs PC |
|---|---|---|---|---|
| float32, portable kernels, ExecuTorch built `-Os` (Lab 3 build) | 152.07 | 171,684 B | 33,312 B | max diff 1.9e-6, top gesture right 6/6 |
| float32, ExecuTorch + firmware `-O3` | 64.50 | 435,340 B | 33,296 B | 1.9e-6, 6/6 |
| float32, `-O3`, batch norm folded into the convs | 63.97 | 432,532 B | 33,296 B | 1.0e-6, 6/6 |
| int8, CMSIS-NN ops (cortex_m backend), `-O3` | 21.3 – 25.5 ¹ | ~181,800 B | 41,868 B | 6/6 |
| **int8, CMSIS-NN, built without `-mno-unaligned-access`** | **3.43** | **117,168 B** ² | **41,848 B** | 6/6 (see below) |

¹ Changed between builds with code layout: most of the time went to `memcpy` calls (next row).
² The committed `firmware/model_test` build. The same model in the earlier test build was 179,272 B (it linked more unused kernels) and ran in 3.437 ms.

The model file is 22,072 B (int8) or 30,776 B (float, folded). Of the int8 RAM: 3,008 B of activations are needed (12 KB reserved), 4,688 B of method memory are used (16 KB reserved), plus 8 KB CMSIS-NN scratch, 2.4 KB input buffer and 1 KB log buffer. So real use is well under the 41 KB shown.

**int8 output on the board vs the PC:** scores differ from the float PyTorch model by up to 0.39 and from ExecuTorch's Python int8 reference by up to 0.25 (CMSIS-NN rounds differently). The top gesture matched the float model on all 6 test windows.

How the speed-ups were found: sampling the program counter by halting the core ~300 times through OpenOCD. In the 21 ms build, ~70% of samples were in `memcpy` called from `arm_nn_mat_mult_kernel_s8_s16`. Details and the other fixes: [firmware/model_test/README.md](../firmware/model_test/README.md#things-that-took-a-while-to-find).

## int8 vs float accuracy (Nov 13, on the PC)

Leave one person out, the same training as `train.py` (seed 0, 30 epochs). The int8 model is calibrated on windows from the training people only. Scored with ExecuTorch's Python int8 reference ops, not on the board.

| Never saw | float | int8 | same answer |
|---|---|---|---|
| P01 | 95.2% | 95.4% | 99.6% of windows |
| P02 | 95.2% | 95.2% | 99.8% of windows |

Per gesture, int8 is within 1 point of float (P01: nothing 97 vs 96, place 90 vs 91, swap_left 94 vs 93; P02: identical). The model trained on everyone: 99.58% right on its own training windows in both float and int8, and the same answer on 99.98% of all 5,426 windows.

## Not measured yet

- Stack use (16 KB reserved in the linker script).
- The full pipeline around the model: sensor read, scaling, window buffer, decoder.
- False triggers per minute on a live stream; latency from motion to click.
- Power.
- Accuracy of the int8 model *on the board* over a whole dataset (only 6 windows were run on the board).

## Finding the gesture inside each cue (Nov 14, on the PC)

No new data, no model change: only where `training/dataset.py` cuts the training and test windows. The old rule took the first motion after a cue. In about 15 cues the hand was still drifting back from the previous gesture when the cue appeared, so the windows were cut on that drift instead of the gesture. The new rule (`find_gesture`) takes the strongest burst of motion in the cue (for mine, the longest).

Leave one person out, `small` max-pool model, 3 seeds, average of per-gesture accuracy:

| | per 1 s window | per gesture (all windows of a cue voted together) | sword | swap_left | swap_right | place |
|---|---|---|---|---|---|---|
| first motion after the cue (old) | 95.0 ± 0.5 | 95.7 ± 0.4 | 91 | 95 | 92 | 96 |
| strongest burst (new) | 97.3 ± 0.3 | 97.7 ± 0.2 | 96 | 99 | 95 | 95 |
| strongest burst, 2 more mistaken cues skipped | 97.7 ± 0.2 | 98.1 ± 0.2 | 97 | 100 | 96 | 96 |

Caveat: the test windows are cut by the same rule, so part of the gain is the test no longer scoring hand drift as a gesture. It says the model was better than the old number showed and that training now sees cleaner examples. It does not say how the board behaves on a live stream; that needs the false-triggers-per-minute replay.

Tried on the same data and within noise of the old baseline (so not adopted): mirroring left/right flicks, speed-change augmentation, mixup, 3-model ensemble, medium width, dropping the two oldest sessions.

Two more cues are now skipped as recording mistakes (P01 s04 cue 29: swap_left cue flicked right; P02 s02 cue 7: no motion). Still wrong in every run and worth a look by the team: P02 s02 cue 26 and s15 cue 11 (swap_right cues that look like a sword/chop), P01 s06 cue 38 and s05 cue 13 (swap_right), P01 s03 cue 2, s05 cue 10 and P02 s15 cue 20 (sword read as mine), and three very weak P02 place jabs (s02 cue 22, s07 cue 9).

## Replaying whole recordings like the board would (Nov 14, on the PC)

`python training/evaluate_stream.py`: train on everyone but one person, slide a 1 s window over that person's whole sessions every 50 ms, run the decoder, compare what fired with what was cued. 3 seeds, 2 test people, 1,410 gesture cues, 125 minutes of idle cues and gaps between cues (idle here is mostly holding still, not looking around).

| Decoder | right gesture fired | fired twice | wrong gesture also fired | false triggers per minute |
|---|---|---|---|---|
| old: confidence ≥ 0.80 twice in a row, 500 ms cooldown, mine drops at once | 90.9% | 6.6 per 100 | 1.8 per 100 | 0.03 |
| **new: ≥ 0.60 three times in a row, 300 ms cooldown, re-arm rule, mine released after 400 ms** | **96.7%** | 0.2 per 100 | 1.9 per 100 | 0.10 |

Per gesture with the new decoder: sword 98, mine 94, place 96, swap_left 100, swap_right 95. The old decoder missed mostly place (80%) and swap_right (89%), whose confidence often stays under 0.80.

The re-arm rule: after a gesture fires it cannot fire again until the model has named something else 3 runs in a row. One swing stays inside the 1 s window for up to a second, so without the rule it can fire again once the cooldown ends.

Other settings tried (same replay): threshold 0.5 catches 97.2% but doubles the wrong gestures (4.5 per 100); 0.7 drops place to 89%; cooldown 200 to 500 ms and re-arm 2 to 5 runs make no difference. Training changes tried against this replay and not adopted: wider time shifts of the training windows (+0.3 points, within noise), no label smoothing (same curve, shifted).

Remaining errors: sword cues that also send a swap_right (5 of 288) or swap_right cues that also send a sword (6 of 282); mine cues with a swap_right at the start (6 of 282); 16 missed mines, mostly the early single-arc ones.

Not measured: this decoder on the board with a live sensor, and false triggers while really looking around (needs the `--look-minutes` recordings).


## Gesture start near the peak, 8 more mistaken cues (Nov 14, on the PC)

**Start of the gesture.** When the hand never stops between the previous motion and the gesture, the strongest burst starts long before the swing (P01 s06 cue 38 peaked 0.9 s after its "start"). One-shot gestures now start at most 0.4 s before the burst's strongest point (`PEAK_LEAD = 40` in `dataset.py`). Leave one person out, `small` max-pool, 3 seeds, % of all test windows right: 98.18% → 98.43% (0.3 s: 98.19%, 0.2 s: 97.86%). That's within noise, but it fixed P01 s06 cues 38 and 55, which were wrong in every run before.

**Eight more mistaken cues skipped** (the ones listed above as worth a look). Each was plotted over its whole span: motion strength, detected start, windows cut, next cue. All are in `BAD_CUES` with what the plot shows: P01 s03 cue 2 and s05 cue 10 (sword cues, ~2 s of repeated swings like chopping), P01 s05 cue 13 (the swing comes after the next cue), P02 s02 cue 22 and s07 cue 9 (place cues with almost no motion), P02 s02 cue 26 and s15 cue 11 (swap_right cues that look like a sword), P02 s15 cue 20 (sword cue, 2+ s of chopping). Windows: **99.06%** right (seeds 99.3 / 99.4 / 98.5; P01 99.0, P02 99.1), every gesture 98–100% recall.

Replay (`evaluate_stream.py`, default decoder, seeds 0 / 1 / 2), before → after both changes:

| | right gesture fired | wrong gesture also fired (per 100 cues) | false triggers / min |
|---|---|---|---|
| before | 97.0 / 97.2 / 95.7% | 1.9 / 2.1 / 1.7 | 0.07 / 0.14 / 0.10 |
| after | 98.1 / 98.3 / 97.4% | 1.7 / 1.5 / 2.6 | 0.10 / 0.10 / 0.07 |

Sword and swap_right now hit 99–100%. Mine is the weakest gesture (93–94%, 6–7 misses). Wrong-gesture events and false triggers didn't change.

Caveat: the 8 cues were found *because* every model got them wrong, so dropping them also removes them from the test (470 → 462 scored gesture cues in the replay, 40 fewer windows). Part of the gain is that. Each cue also has a reason that shows in the raw motion. On the cleaned data, these were tried and not adopted (all within noise of 99.06%): 60 epochs 99.35%, speed-change augmentation 99.00%, distilling a 3-model ensemble 99.07%, avg + max pooling 98.95%, medium avg + max 99.40%, residual blocks 98.93%.
