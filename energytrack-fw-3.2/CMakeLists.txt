# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Placa: Pico W (CYW43). Precisa vir antes do import do SDK.
set(PICO_BOARD pico_w CACHE STRING "Board type")

# pico-sdk >= 2.0 (pio_claim_free_sm_and_add_program_for_gpio_range)
include(pico_sdk_import.cmake)

# FreeRTOS-Kernel (fork raspberrypi/FreeRTOS-Kernel ou >= V11 com porta RP2040)
if (NOT FREERTOS_KERNEL_PATH AND DEFINED ENV{FREERTOS_KERNEL_PATH})
    set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
endif()
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(energytrack_fw C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# ---------------------------------------------------------------------------
# Self-test de periféricos (WPS 3.2). No firmware principal, adicione os
# fontes de src/hal ao alvo existente e reaproveite as mesmas linhas de
# pico_generate_pio_header / pico_enable_stdio_*.
# ---------------------------------------------------------------------------
add_executable(energytrack_selftest
    app/selftest_main.c
    src/hal/periph_init.c
    src/hal/ws2812.c
    src/hal/oled_probe.c
    src/hal/adc_shared.c
    src/hal/pzem_link.c
    src/hal/pzem_modbus.c
)

pico_generate_pio_header(energytrack_selftest ${CMAKE_CURRENT_LIST_DIR}/src/hal/ws2812.pio)

target_include_directories(energytrack_selftest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src/hal
    ${CMAKE_CURRENT_LIST_DIR}/config          # FreeRTOSConfig.h
)

target_compile_definitions(energytrack_selftest PRIVATE SELFTEST_USE_CYW43=1)
target_compile_options(energytrack_selftest PRIVATE -Wall -Wextra)

target_link_libraries(energytrack_selftest
    pico_stdlib
    hardware_uart
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_pio
    hardware_clocks
    pico_cyw43_arch_none        # firmware de rede usará pico_cyw43_arch_lwip_sys_freertos
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

# stdio SOMENTE por USB: a uart0 (GP0/GP1) pertence ao PZEM.
pico_enable_stdio_usb(energytrack_selftest 1)
pico_enable_stdio_uart(energytrack_selftest 0)

pico_add_extra_outputs(energytrack_selftest)
