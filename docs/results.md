# Results

Measured numbers only, each with how it was measured. Add new results at the top of their section.

## Model on the board (Nov 13)

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
