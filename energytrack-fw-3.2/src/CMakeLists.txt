# Componente principal do EnergyTrack (ESP-IDF).
#
# ET_APP escolhe o firmware (platformio.ini, board_build.cmake_extra_args):
#   energytrack -> WPS 3.1, aquisição e processamento em tempo real
#   selftest    -> WPS 3.2, self-test do PZEM, LED e log serial
# Os dois usam os mesmos componentes do ESP-IDF, então os REQUIRES não mudam.
if(NOT DEFINED ET_APP)
    set(ET_APP energytrack)
endif()

set(ET_HAL_COMMON
    hal/board_init.c
    hal/pzem_link.c
    hal/pzem_modbus.c
)

if(ET_APP STREQUAL "energytrack")
    set(ET_SRCS
        ../app/energytrack_main.c
        ${ET_HAL_COMMON}
        hal/acq_timer.c
        hal/adc_dma.c
        hal/adc_frame.c
        proc/temp_sensor.c
        proc/energy.c
        proc/goal.c
        proc/anomaly.c
        proc/processing.c
        rt/rt_monitor.c
        rt/rt_tasks.c
    )
elseif(ET_APP STREQUAL "selftest")
    set(ET_SRCS
        ../app/selftest_main.c
        ${ET_HAL_COMMON}
    )
else()
    message(FATAL_ERROR "ET_APP deve ser 'energytrack' ou 'selftest' (veio '${ET_APP}')")
endif()

idf_component_register(
    SRCS ${ET_SRCS}
    INCLUDE_DIRS ../include hal proc rt
    REQUIRES esp_driver_uart esp_driver_gpio esp_driver_gptimer esp_adc esp_timer freertos
)

# Avisos rigorosos só nos fontes do projeto: o ESP-IDF compila junto, mas não é nosso.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
