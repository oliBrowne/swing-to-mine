# firmware/model_test

Milestone M4: runs the trained gesture model on the board with ExecuTorch and measures it. It is a test bench, not the controller: no sensor, no USB. It loops over one real recorded window per gesture, times each inference with the DWT cycle counter, and keeps the results in RAM variables that you read over the ST-LINK, so no UART is needed. The green LED toggles on every inference. If something fails, the red LED blinks.

**Status (Nov 13):** works on the board. The int8 model (max pooling) takes **3.41 ms** per inference at 168 MHz, uses 117 KB of flash and 41 KB of RAM, and picks the same gesture as the float PyTorch model on all 6 test windows. Numbers and how they were measured: [docs/results.md](../../docs/results.md).

## What you need

Linux or WSL with the Lab 3 setup:

- ExecuTorch 1.3.0 source in `~/executorch/executorch`, its Python env in `~/executorch/et-env`, CMSIS-NN in `~/executorch/CMSIS-NN`
- `arm-none-eabi-gcc` (13.2 used), CMake ≥ 3.20, Ninja, OpenOCD 0.12
- `pip install pandas` in the env (for `training/dataset.py`)
- a trained model: `python training/train.py` → `training/out/gesture_small.pt`

Other locations: pass `-DET_ROOT=… -DET_BUILD=… -DCMSIS_NN=… -DTORCH_INCLUDE=…` to cmake.

## Build, flash, read

```bash
source ~/executorch/et-env/bin/activate

# 1. Export the model (int8, CMSIS-NN): writes training/out/model_test/{model.pte,test_windows.h,...}
python training/export.py

# 2. Cross-build ExecuTorch + the CMSIS-NN ops once (~3 min), into ~/executorch/executorch-build/cortex-m4-cmsis
bash firmware/model_test/scripts/build_executorch.sh

# 3. Build the firmware
cd firmware/model_test
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-gcc-toolchain.cmake
cmake --build build                    # prints the size; build/model_test.elf + .bin

# 4. Flash (from WSL: first `usbipd attach --wsl --busid <BUSID>` in Windows; `usbipd list` shows the BUSID)
bash scripts/flash.sh

# 5. Read the results from the running board
python scripts/readback.py
```

`readback.py` prints the time per inference, the board's scores for each test window next to the expected ones, and whether the top gesture matches the float model. When you're done in WSL, run `usbipd detach --busid <BUSID>` in Windows to give the ST-LINK (and its COM port) back to Windows.

**Float version** (portable kernels only, for comparison): run `python training/export.py --float`, then build ExecuTorch with the float ops (`OPS=aten::convolution.out,aten::relu.out,aten::mean.out,aten::permute_copy.out,aten::addmm.out bash scripts/build_executorch.sh <other_dir>`), then configure with `-DCORTEX_M=OFF -DMODEL_DIR=../../training/out/model_test_float -DET_BUILD=<other_dir>`.

## Things that took a while to find

- **No `-mno-unaligned-access`.** ExecuTorch's `cmake/toolchains/arm-none-eabi.cmake` adds it. CMSIS-NN reads 4 bytes at a time through `memcpy`, and with this flag each read becomes a real `memcpy` call. That was ~70% of the inference time (21.5 ms instead of 3.4 ms), found by sampling the PC over the debugger. The Cortex-M4 does unaligned loads in hardware, so `build_executorch.sh` sets `CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` itself to leave the flag out.
- **`-O3`, not `-Os`.** The Lab 3 ExecuTorch build is `MinSizeRel`. Float went from 152 ms to 64.5 ms just from that. It was already hard-float.
- **`-fno-tree-loop-distribute-patterns` on any hand-written `memcpy`/`memset`.** At `-O2`/`-O3`, GCC recognises the copy loop and replaces it with a call to `memcpy`, i.e. the function calls itself until the stack runs off the bottom of RAM (HardFault, `MSP` below `0x20000000`). `src/fast_mem.c` is built with that flag (`../capture` does the same).
- **Patched `quantized_avg_pool2d`.** ExecuTorch 1.3.0's `backends/cortex_m/ops/op_quantized_avg_pool2d.cpp` passes CMSIS-NN no scratch buffer. That's fine on Cortex-M55 (MVE), but the M4's DSP code path needs `output_w × channels × 4` bytes and returns `ARM_CMSIS_NN_ARG_ERROR` (the method fails with error 1). `src/op_quantized_avg_pool2d_patched.cpp` is the same op taking the buffer from the temp allocator, like the conv op does. Linking it replaces the library's version.
- **Export changes** (in `training/export.py`): Conv1d → Conv2d with a (1, k) kernel on a channels-last `[1, 6, 1, 100]` input (CMSIS-NN ops are 2D), and the pooling over time → `MaxPool2d((1, 13))` (or `AvgPool2d` for `--pool avg` models), because ExecuTorch's float `mean` rejects the channels-last tensor the int8 convs produce. Same maths: the rewrite matches the original model to 3e-6 in float.
- **Startup** has to turn the FPU on and run C++ constructors: ExecuTorch registers its kernels from constructors. Without them you get `OperatorMissing` (0x14).
- **Debugging without a UART:** build ExecuTorch with `-DEXECUTORCH_ENABLE_LOGGING=ON` and its log lands in the `g_log` array (`et_pal_emit_log_message` in `main.cpp`). Dump it with OpenOCD: `dump_image log.bin <address of g_log> 1024`.

## Files

| File | What it is |
|---|---|
| `src/main.cpp` | loads the .pte, runs the test windows in a loop, times them, stores results in `g_*` |
| `src/board.c/.h` | 168 MHz clock from the 8 MHz crystal, flash cache, LEDs, 1 ms tick (registers, like `../capture`) |
| `src/startup.c` | vector table, FPU on, .data/.bss, C++ constructors |
| `src/syscalls.c` | the few C library hooks (heap, no files) |
| `src/fast_mem.c` | word-at-a-time `memcpy`/`memset` |
| `src/op_quantized_avg_pool2d_patched.cpp` | the fixed ExecuTorch op (see above) |
| `stm32f407vg.ld` | memory map: 1 MB flash, 128 KB RAM, 16 KB stack |
| `scripts/build_executorch.sh` | cross-builds ExecuTorch + CMSIS-NN ops with the right flags |
| `scripts/flash.sh` | OpenOCD over the ST-LINK |
| `scripts/readback.py` | reads `g_*` from the running board, compares with the PC's scores |
