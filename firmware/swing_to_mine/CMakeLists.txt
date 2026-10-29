cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
project(swing_to_mine C ASM)

set(LIB ${CMAKE_CURRENT_LIST_DIR}/../lib)
set(HAL ${LIB}/stm32f4xx_hal_driver)
set(TUSB ${LIB}/tinyusb/src)

add_executable(swing_to_mine.elf
  Core/Src/main.c
  Core/Src/stm32f4xx_it.c
  App/button.c
  App/hid_out.c
  App/usb_descriptors.c
  ${LIB}/cmsis_device_f4/Source/Templates/system_stm32f4xx.c
  ${LIB}/cmsis_device_f4/Source/Templates/gcc/startup_stm32f407xx.s
  ${HAL}/Src/stm32f4xx_hal.c
  ${HAL}/Src/stm32f4xx_hal_cortex.c
  ${HAL}/Src/stm32f4xx_hal_gpio.c
  ${HAL}/Src/stm32f4xx_hal_pwr_ex.c
  ${HAL}/Src/stm32f4xx_hal_rcc.c
  ${TUSB}/tusb.c
  ${TUSB}/common/tusb_fifo.c
  ${TUSB}/device/usbd.c
  ${TUSB}/class/hid/hid_device.c
  ${TUSB}/portable/synopsys/dwc2/dcd_dwc2.c
  ${TUSB}/portable/synopsys/dwc2/dwc2_common.c
)

target_include_directories(swing_to_mine.elf PRIVATE
  Core/Inc
  App
  ${LIB}/cmsis_core/CMSIS/Core/Include
  ${LIB}/cmsis_device_f4/Include
  ${HAL}/Inc
  ${TUSB}
)

target_compile_definitions(swing_to_mine.elf PRIVATE
  STM32F407xx
  USE_HAL_DRIVER
  HSE_VALUE=8000000
  CFG_TUSB_MCU=OPT_MCU_STM32F4
)

set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

target_compile_options(swing_to_mine.elf PRIVATE
  ${CPU_FLAGS} -Og -g3 -Wall -ffunction-sections -fdata-sections
)

target_link_options(swing_to_mine.elf PRIVATE
  ${CPU_FLAGS}
  -T${CMAKE_CURRENT_LIST_DIR}/STM32F407VGTx_FLASH.ld
  --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=swing_to_mine.map -Wl,--print-memory-usage
)

add_custom_command(TARGET swing_to_mine.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary swing_to_mine.elf swing_to_mine.bin
  COMMAND ${CMAKE_SIZE} swing_to_mine.elf
)
