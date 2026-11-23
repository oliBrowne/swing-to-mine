# Results

Measured numbers only, each with how it was measured. Add new results at the top of their section.

## Pruning, int8 and simulated speed per model size (Nov 23, PC + Renode, no board)

Full method, all tables and the board test plan: [compression-and-speed.md](compression-and-speed.md). Leave one person out over the 7 testable people, 3 seeds (seed-to-seed spread of the 7-person average ≈ ±0.6 points), `training/prune.py`. int8 = the board's CortexMQuantizer, PyTorch reference on the PC. Time: Renode 1.15.3 instruction count × 1.43 cycles per instruction (the ratio of the full model's 573,250 board cycles to its 400,000 Renode instructions), so only the full model's time is measured.

| Model (channels) | Params | float | int8 | Replay: right gesture | False / min | Flash (firmware) | Time per run |
|---|---|---|---|---|---|---|---|
| full (16/24/32) | 6,774 | 97.0% | 97.0% | 94.7% | 0.10 | 116,912 B | 3.41 ms (board) |
| 75%, from scratch | 4,002 | 97.3% | 97.4% | 95.4% | 0.16 | 111,024 B | ~2.50 ms (sim) |
| 75%, bn-pruned + 10 ep | 4,002 | 96.2% | 96.2% | 93.4% | 0.12 | 111,024 B | ~2.50 ms (sim) |
| 50%, from scratch | 1,950 | 95.9% | 95.7% | 93.3% | 0.21 | 106,416 B | ~1.70 ms (sim) |
| 50%, bn-pruned + 10 ep | 1,950 | 94.8% | 94.8% | 92.1% | 0.22 | 106,416 B | ~1.70 ms (sim) |
| 25%, from scratch | 618 | 94.2% | 94.2% | 91.4% | 0.44 | 103,408 B | ~1.11 ms (sim) |
| 25%, bn-pruned + 10 ep | 618 | 88.9% | 90.0% | 83.6% | 0.28 | 103,408 B | ~1.11 ms (sim) |

L1 and random channel picks were within noise of batch-norm scale except at 25%. With 30 epochs of fine-tuning (seed 0 only), pruned models match training from scratch at 50% and 35% and the best one (bn) comes within ~1 point at 25% (L1 stays 3.6 behind). RAM (`data`+`bss`) is 41,848 B for every size (fixed pools). Renode profile of the full model: 94% of instructions are in the convolutions, about half of that gathering input data rather than multiplying.

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

## Mine "misses" were a scoring mistake (Nov 14, on the PC)

Most missed mine cues had the model saying mine for 70 to 90 runs in a row at up to 0.95 confidence. The hold had started during the previous cue (people keep chopping past the cue) and was still on, so no new "mine started" event fell inside the cue. In the game that is correct: it is still mining. `evaluate_stream.py` now counts a mine cue as hit when mine was held for at least 0.5 s during it.

Replay with that scoring, 3 seeds, 2 unseen people, 1,386 gesture cues: right gesture fired **98.8%** (sword 100, mine 98, place 97, swap_left 100, swap_right 99), fired twice 0.3 per 100, wrong gesture also fired 1.9 per 100, false triggers 0.12 per minute.

What is left: a place firing during a mine cue (12 of 282, at the start or end of the chopping), a swap_right at the start of a mine (6 of 282), 9 missed place jabs of 279. Mine release time 200 to 600 ms makes no difference; 800 ms starts to swallow the next gesture.

## Third person: P10 (Nov 14, evening, on the PC)

New data: P10, sessions s30, s31, s32 (70 cues each, 10 per gesture incl. jump and idle), recorded with `host/record.py` on COM7. 6,614 windows in total.

**Board restart in s32.** About 92 s into s32 the board's clock jumped from 2,150,375 ms back to 239 ms (the samples just before show a very hard swing, so probably a brown-out or loose wire). The cues in the labels file follow the board clock too. The committed s32 files are repaired (times after the restart shifted to keep counting up, with a 2 s gap), and the swap_left cue cut in half by the restart (cue 24) is in `BAD_CUES`. As a safety net for future recordings, `dataset.py` also stitches any restart it finds back onto one clock with a 10 s gap (`_reset_offsets`, `load_labels`); `evaluate_stream.py` uses the same loader.

Leave one person out (`train.py`, `small` max-pool, seed 0), % of 1 s windows right on the unseen person:

| Never saw | windows right | weakest gestures |
|---|---|---|
| P01 | 99.4% | nothing 99% |
| P02 | 99.7% | nothing 100% |
| **P10 (new)** | **96.1%** | sword 74% (24 of 140 windows read as nothing, 12 as place), place 93% (11 of 150 read as mine) |

Replay (`evaluate_stream.py`, default decoder, seeds 0 / 1 / 2), now with 3 unseen people, 612 gesture cues and 46.7 idle minutes per seed:

| | right gesture fired | wrong gesture also fired (per 100 cues) | false triggers / min |
|---|---|---|---|
| P01 + P02 (before, mine scoring fix) | 98.8% (3 seeds pooled) | 1.9 | 0.12 |
| P01 + P02 + P10 | 96.4 / 96.4 / 94.8% | 2.3 / 7.9 / 1.1 | 0.11 / 0.06 / 0.06 |

Per gesture (seeds 0 / 1 / 2): sword 92 / 94 / 93, mine 98 / 98 / 98, place 92 / 93 / 85, swap_left 100 / 100 / 100, swap_right 100 / 98 / 98. Seed 1's 7.9 wrong-per-100 is almost all one pattern: a swap_left firing during swap_right cues (39 times), i.e. the flick back to center read as the opposite flick. Seeds 0 and 2 don't do it, so it's a model that happens to land there, worth watching.

P10 alone (seed 0, replay): sword 20 of 30 cues, place 22 of 30, mine and both swaps all hit, about 88% overall.

The drop is the new person, not the old ones: P01 and P02 are still 99%+ per window. P10's swords are gentler than P01/P02's, so a model that only saw two people calls many of them "nothing", and some of P10's place jabs look like a chop. That is the "more people's data" problem the architecture sweep pointed at; P10's own data is now in the everyone-model (`training/out/gesture_small.pt`, not committed), so the board model should do better on P10 than this test says. More people (P11 next) is the fix to try before touching the model.

Not done here: int8 export and the board run for the new model.

## Fourth person: P11 (Nov 14, night, on the PC)

New data: P11, sessions s33 (stopped early, 18 cues), s34 (70), s35 (56), s36 (66): 210 cues, no board restarts, one 59 ms gap. 1,131 new windows, 7,745 in total.

The four people swing very differently. Median peak rotation speed during the cue (deg/s):

| | sword | mine | place |
|---|---|---|---|
| P01 | 390 | 342 | 98 |
| P02 | 438 | 289 | 128 |
| P10 | 211 | 202 | 162 |
| P11 | 916 | 410 | 228 |

Leave one person out (`train.py`, `small` max-pool, seed 0), % of 1 s windows right on the unseen person:

| Never saw | 3 people (P01, P02, P10) | 4 people (+ P11) | weakest now |
|---|---|---|---|
| P01 | 99.3% | 99.1% | place 98% |
| P02 | 99.8% | 99.8% | |
| P10 | 95.2% | **96.2%** | sword 67% (19 → nothing, 17 → place, 10 → mine) |
| **P11 (new)** | | **95.0%** | sword 90% (10 → swap_right), swap_left 90% (16 → swap_right), place 91% (12 → mine) |

Adding P11 helped P10's place a lot (89% → 99%) but not P10's slow swords. P11's errors are the opposite kind: hard flicks that the model also reads as the opposite flick (the wrist bouncing back).

Replay (`evaluate_stream.py`, default decoder, seeds 0 / 1 / 2), 4 unseen people, 761 gesture cues and 49.8 idle minutes per seed:

| | right gesture fired | wrong gesture also fired (per 100 cues) | false triggers / min |
|---|---|---|---|
| P01 + P02 + P10 | 95.9 / 96.2 / 94.1% | 2.3 / 3.8 / 0.7 | 0.09 / 0.00 / 0.13 |
| P01 + P02 + P10 + P11 | 95.9 / 96.3 / 94.9% | 3.7 / 7.1 / 3.5 | 0.12 / 0.08 / 0.04 |

Per gesture (seeds 0 / 1 / 2): sword 90 / 91 / 88, mine 98 / 98 / 98, place 94 / 97 / 92, swap_left 97 / 98 / 98, swap_right 100 / 98 / 99. Most of the extra wrong gestures are swap_left cues that also fire swap_right (11 / 18 / 4) and the other way round.

No cues of P10 or P11 have been checked by hand yet (P01/P02 had 17 removed as recording mistakes, see above), so their numbers are not directly comparable to P01/P02's.

## Recording mistakes in P10 / P11 (Nov 14, night, on the PC)

Same check P01/P02 got: the cues that leave-one-person-out models (seeds 0, 1, 2) got mostly wrong, 15 of 381 (P10 s30-s32, P11 s33-s36), each plotted next to good cues of the same person and gesture. Only clear mistakes go into `BAD_CUES`; unusual but real gestures stay in.

- Skipped (6): P10 s32 #59, #70 (sword cue, a short forward jab that looks like his own place); P11 s33 #10 and s34 #32 (sword cue, a single flick like swap_right); P11 s33 #17 and s34 #24 (swap_left cue, flicked right first).
- Kept (9): P10's slow or wound-up swords (real, just slow: exactly what the model needs to learn), P10 s30 #40 (small, choppy motion, unclear), P11 s34 #26 (left flick then a big right flick, unclear), P11 s33 #4 (weak sword), P11's two hard place jabs read as mine.

| | before | after |
|---|---|---|
| never saw P10 (windows, seed 0) | 96.2% | 97.3% (sword 75%) |
| never saw P11 (windows, seed 0) | 95.0% | 97.2% (sword 94, place 91, swap_left 97) |
| replay, 4 people, seeds 0 / 1 / 2 | 95.9 / 96.3 / 94.9% | 95.9 / 97.1 / 96.8% |
| wrong gesture also fired, per 100 cues | 3.7 / 7.1 / 3.5 | 2.1 / 5.8 / 2.0 |
| false triggers / min | 0.12 / 0.08 / 0.04 | 0.06 / 0.04 / 0.04 |

P10's slow swords are still the weak spot: that's a real gap, not bad data.

## More P10, and the first look-around idle test (Nov 14, night, on the PC)

New P10 sessions: s38 (22 cues, stopped early), s39 (15 sword + 15 place, his weak gestures), s40 (8 cues), s41 + s42 (4.4 min of looking around like in look mode, no cues: all windows count as "nothing"). s37 is left out: about 10 s of data around a 32-minute hole where the board went quiet, and one cue.

**Look-around idle, P10 never seen in training (seeds 0 / 1 / 2):** 4 / 1 / 6 accidental gestures in 4.4 minutes = **0.9 / 0.2 / 1.4 per minute**, all place except 2 swords. That's about 10x the rate measured between cues (0.04-0.16 per minute), so between-cue idle was flattering: slow looking around is a harder test. His gentle movements look like his own place jab.

With all P10 data, leave one person out, seed 0: P10 96.1% of windows (sword 81%, place 96%), P11 96.8%, P01 99.4%, P02 99.9%.

Replay, 4 people, seeds 0 / 1 / 2: right gesture fired **93.6 / 92.8 / 94.4%** (sword 85 / 84 / 92, place 91 / 88 / 88), false triggers 0.09 / 0.04 / 0.16 per minute over 55 idle minutes. Lower than before because s39 adds 30 more of P10's hardest cues (slow swords, places) to the test, not because the model got worse on anyone else.

Next: look-around idle from more people (only P10 has it), and see whether training on it brings the place false triggers down.

## Controller without hardware (Nov 22, on the PC)

`firmware/swing_to_mine` (USB keyboard + mouse, air mouse, modes) was built and checked with no board, so **none of these numbers come from real hardware**.

- **Size** (`arm-none-eabi-size`, no model): 20.9 KB flash, 3.7 KB RAM. With the model expect roughly the live_test size on top (~105 KB); not built here because ExecuTorch wasn't installed.
- **Logic tests** (`make -C firmware/swing_to_mine/test`): 82 checks on synthetic sensor data and button presses, all pass.
- **Decoder parity** (`make ... parity`): the firmware's decoder and `decode()` in `training/evaluate_stream.py` fired identical events on 300 random model-output streams (5,425 events).
- **Renode simulator** (`sim/renode_check.sh`, Renode 1.15.3, fake MPU-6050 that reports "upright and still"): boots, reads the sensor ~100 times a second, calibrates into look mode (green), single press → move (orange), hold 1 s → inventory (blue), hold 1 s → back to move. Renode has no USB port model, so USB isn't covered.

**Power-on calibration held in a hand** (Nov 23, PC replay of the recordings, not hardware). The old rule (each gyro axis within 3 °/s for 1.5 s) was met in only 0–10% of the hand-held idle cues, so the LEDs would have kept spinning unless the board lay on a table. New rule: after 3 s use the calmest 1 s block so far, if the gyro stayed within 15 °/s in it. Measured by starting calibration 0.5 s before each recorded cue and comparing the result with the average of the calmest second of the whole session: from the 138 idle cues it finished in 3.9 s on average (longest 15.9 s) and the error was 0.7 °/s on average, never above the 3 °/s dead zone; from all 1,082 cues (so mostly while a gesture is being made) the error was above the dead zone in 2% of starts, 5.8 °/s at worst. Without the 15 °/s limit, 9% of idle starts and 18% of all starts ended above the dead zone (worst 56 °/s), which would show as a drifting camera.

**Recordings replayed through the air mouse** (`test/replay`, look mode, no model, so mining doesn't freeze the camera here the way it does once `mine` fires). "Camera" is how fast the view would turn, converting mouse counts at Minecraft's default sensitivity (~0.15° per count, my estimate), averaged over each labelled stretch; "frozen" is the share of time the camera was frozen for fast motion. Look-around = P10 s41 + s42 (4.5 min); gesture cues = P01 s05, P02 s07, P10 s30, P11 s35.

| Freeze above | Look-around: camera | Look-around: frozen | Sword cues: camera | Mine cues: camera | Place cues: camera | Between cues: frozen |
|---|---|---|---|---|---|---|
| 120 °/s | 23 °/s | 14% | 4 °/s | 10 °/s | 10 °/s | 10% |
| 180 °/s | 33 °/s | 6% | 8 °/s | 26 °/s | 13 °/s | 7% |
| **250 °/s** (default, from controls.md) | 41 °/s | 2% | 17 °/s | 48 °/s | 17 °/s | 5% |

What it means: at the default 250 °/s, ordinary looking around is almost never frozen, but gestures still drag the camera noticeably before the freeze kicks in (a sword swing moves the view, the place jab barely rotates the board so the freeze rarely triggers). Lowering it to ~180 °/s roughly halves the drag during swings and freezes normal looking 6% of the time. This is a feel question to settle in the first play test (`FREEZE_DPS` in `firmware/swing_to_mine/src/config.h`). The hotbar flicks hardly move the camera at any setting, because they turn the board around the handle's forward axis, which the air mouse ignores.

## Eight people, and the controller with the model in the simulator (Nov 23, on the PC)

**Training with Eli's new people** (P05, P06, P07 and more P01, pushed Nov 22; `python training/train.py`, default settings, seed 0, 11,126 windows from 8 people, ~8 min on 4 CPU cores). Leave one person out, share of their windows right: P01 99.2%, P02 99.7%, P05 95.5%, P06 97.1%, P07 94.3%, P10 95.4%, P11 93.6% (P03 has only 26 windows, training only). Per class, averaged over the unseen people: nothing 99%, sword 91%, mine 96%, place 94%, swap_left 97%, swap_right 98%. Most of Eli's new sessions haven't had their bad cues marked yet (his commit says so), so the new people's numbers may go up once that's done. The model trained on everyone, exported int8 (`training/export.py`, ExecuTorch 1.3.1, torch 2.12), gives the same top class as the float model on 100% of the 11,126 windows; the `.pte` is 21.8 KB.

**Controller firmware with the model linked in** (`SWING_MODEL=ON`, arm-none-eabi-gcc, `--print-memory-usage`): 122.9 KB flash, 44.7 KB RAM (of 1 MB / 128 KB).

**End to end in the simulator** (`firmware/swing_to_mine/sim/renode_gestures.sh`, Renode 1.15.3): cues 1–20 of P02 s10 (65.5 s) played through the fake MPU-6050 at 100 Hz into the full firmware, with the int8 model running on the simulated Cortex-M4 every 50 ms (1,358 runs, none failed). This session was part of the training data, so this checks that the firmware does what the PC does, not how accurate the model is.

| | sword | mine | place | swap_left | swap_right |
|---|---|---|---|---|---|
| cued | 4 | 2 | 2 | 4 | 4 |
| PC: float model + `decode()` on the same samples | 4 | 2 | 3 | 4 | 5 |
| firmware in the simulator: decoder events | 4 | 2 | 3 | 4 | 5 |
| firmware: handed to USB | 4 left clicks | 2 left holds (5.8 s held in total) | 3 right clicks | 9 scroll steps, sum −1 (4 up, 5 down) | |

The firmware fired exactly what the PC version fired. The one extra `place` and `swap_right` are mistakes the model also makes on the PC (the piece has a jump cue and 3 idle cues). Not measured here: inference time inside this firmware (Renode doesn't model the chip's timing; `firmware/model_test` measured 3.4 ms on the board) and anything about USB.

