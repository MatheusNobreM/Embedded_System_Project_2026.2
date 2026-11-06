# EnergyTrack — projeto ESP-IDF, compilado pelo PlatformIO (ver platformio.ini).
#
# O componente principal é a pasta src/ (src/CMakeLists.txt). Qual firmware
# entra no build vem de ET_APP (energytrack ou selftest), que cada ambiente do
# platformio.ini passa por board_build.cmake_extra_args.
# Configuração do ESP-IDF e do FreeRTOS: sdkconfig.defaults.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(energytrack_fw)
