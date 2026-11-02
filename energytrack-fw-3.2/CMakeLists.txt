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

include(pico_sdk_import.cmake)

# FreeRTOS-Kernel: -DFREERTOS_KERNEL_PATH, variável de ambiente ou ~/pico/FreeRTOS-Kernel.
if (NOT FREERTOS_KERNEL_PATH)
    if (DEFINED ENV{FREERTOS_KERNEL_PATH})
        set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
    else()
        file(TO_CMAKE_PATH "${USERHOME}" _et_home)
        set(FREERTOS_KERNEL_PATH ${_et_home}/pico/FreeRTOS-Kernel)
    endif()
endif()
set(_et_freertos_import ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
if (NOT EXISTS ${_et_freertos_import})
    message(FATAL_ERROR "FreeRTOS-Kernel nao encontrado em '${FREERTOS_KERNEL_PATH}'. "
        "Clone com: git clone https://github.com/FreeRTOS/FreeRTOS-Kernel ~/pico/FreeRTOS-Kernel "
        "ou passe -DFREERTOS_KERNEL_PATH=<caminho>.")
endif()
include(${_et_freertos_import})

project(energytrack_fw C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(ET_HAL_COMMON
    src/hal/board_init.c
    src/hal/pzem_link.c
    src/hal/pzem_modbus.c
)

set(ET_FW_SOURCES
    app/energytrack_main.c
    ${ET_HAL_COMMON}
    src/hal/acq_timer.c
    src/hal/adc_dma.c
    src/proc/temp_sensor.c
    src/proc/energy.c
    src/proc/goal.c
    src/proc/anomaly.c
    src/proc/processing.c
    src/rt/rt_monitor.c
    src/rt/rt_tasks.c
)

set(ET_SELFTEST_SOURCES
    app/selftest_main.c
    ${ET_HAL_COMMON}
)

# Avisos rigorosos só nos fontes do projeto: o SDK compila junto, mas não é nosso.
set_source_files_properties(${ET_FW_SOURCES} ${ET_SELFTEST_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Configuração comum aos dois alvos.
function(energytrack_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/src/hal
        ${CMAKE_CURRENT_LIST_DIR}/src/proc
        ${CMAKE_CURRENT_LIST_DIR}/src/rt
        ${CMAKE_CURRENT_LIST_DIR}/config          # FreeRTOSConfig.h
    )
    target_link_libraries(${target}
        pico_stdlib
        hardware_uart
        pico_cyw43_arch_none        # só o LED da placa; firmware de rede usará pico_cyw43_arch_lwip_sys_freertos
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4
    )
    # stdio SOMENTE por USB: a uart0 (GP0/GP1) pertence ao PZEM.
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_add_extra_outputs(${target})
endfunction()

# ---------------------------------------------------------------------------
# WPS 3.1 — firmware de aquisição (UART, ADC, timer, DMA) e processamento
# ---------------------------------------------------------------------------
add_executable(energytrack_fw ${ET_FW_SOURCES})
energytrack_target(energytrack_fw)
target_link_libraries(energytrack_fw hardware_adc hardware_dma hardware_irq)

# ---------------------------------------------------------------------------
# WPS 3.2 — self-test do PZEM, LED e log USB
# ---------------------------------------------------------------------------
add_executable(energytrack_selftest ${ET_SELFTEST_SOURCES})
energytrack_target(energytrack_selftest)
