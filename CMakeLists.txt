cmake_minimum_required(VERSION 3.22)
project(vibemon C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Must be identical for compiling AND linking.
set(CPU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

set(TARGET ${PROJECT_NAME}.elf)

add_executable(${TARGET}
    src/main.c
    src/bsp/clock.c
    third_party/cmsis_device_f4/Source/Templates/gcc/startup_stm32f411xe.s
    third_party/cmsis_device_f4/Source/Templates/system_stm32f4xx.c
)

# stm32f4xx.h uses this to select the device header. Without it: #error.
target_compile_definitions(${TARGET} PRIVATE STM32F411xE)

target_include_directories(${TARGET} PRIVATE
    third_party/cmsis_device_f4/Include
    third_party/CMSIS_5/CMSIS/Core/Include
    src
)

target_compile_options(${TARGET} PRIVATE
    ${CPU_FLAGS}
    -Wall -Wextra
    -ffunction-sections -fdata-sections
    $<$<CONFIG:Debug>:-Og -g3 -gdwarf-4>
    $<$<CONFIG:Release>:-O2 -g>
)

target_link_options(${TARGET} PRIVATE
    ${CPU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/linker/STM32F411RETx_FLASH.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--print-memory-usage
)

add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${PROJECT_NAME}.bin
)