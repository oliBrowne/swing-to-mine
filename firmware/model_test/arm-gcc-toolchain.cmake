# Tells CMake to build for the STM32 (Arm Cortex-M4) instead of your PC.
# Same as ../capture/arm-gcc-toolchain.cmake plus a C++ compiler (ExecuTorch is C++).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)
# Don't try to link a test program for the PC while configuring.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
