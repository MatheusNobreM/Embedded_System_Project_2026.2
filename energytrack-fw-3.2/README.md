# EnergyTrack — WPS 3.2: mapa de pinos e self-test de periféricos

Placa: BitDogLab v7 + Raspberry Pi Pico W (RP2040). Documento de referência: "EnergyTrack — Mapa de Pinos e Periféricos (WPS 3.2)".

## Estrutura

```
include/board_pins.h       mapa de pinos (fonte única; sem dependência do SDK)
include/board_resources.h  uart0/i2c1 + trava contra stdio na UART0
config/FreeRTOSConfig.h    RP2040, núcleo único, FreeRTOS >= V11
src/hal/                   init dos periféricos, WS2812 (PIO), ADC compartilhado,
                           OLED (probe), PZEM (Modbus + transporte)
app/selftest_main.c        firmware de self-test: tudo rodando ao mesmo tempo
test/host/test_pinmap.c    validação do mapa e do protocolo, roda no PC
```

## 1. Teste de host (sem placa)

```bash
mkdir -p build-host
gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal \
    test/host/test_pinmap.c src/hal/pzem_modbus.c -o build-host/test_pinmap
./build-host/test_pinmap      # deve terminar em "RESULTADO: OK"
```

Rode sempre que alguém mexer em `board_pins.h` (vale colocar no CI).

## 2. Build do self-test

Pré-requisitos: pico-sdk 2.x com submódulos `cyw43-driver` e `tinyusb`, FreeRTOS-Kernel (fork `raspberrypi/FreeRTOS-Kernel`), `arm-none-eabi-gcc`.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
export FREERTOS_KERNEL_PATH=~/pico/FreeRTOS-Kernel
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Grave `build/energytrack_selftest.uf2` (BOOTSEL + arrastar o arquivo).

> Se o pico-sdk não tiver o submódulo `tinyusb`, o CMake só avisa e o stdio USB some em silêncio. Rode `git submodule update --init lib/tinyusb`.

## 3. Execução na bancada

1. PZEM ligado ao JST de GP0/GP1 pelo conversor 5 V ↔ 3,3 V, com carga de teste.
2. Abra o monitor serial USB (115200, qualquer valor serve no CDC).
3. Mexa o joystick, aperte A, B e o clique do joystick, deixe rodar 60 s.
4. Aceite quando aparecer `>>> SELFTEST PASS <<<`.

Sinais esperados: LED RGB verde (PZEM ok) / vermelho (sem PZEM) / azul (botão); pixel da matriz seguindo o joystick; OLED piscando a cada 0,5 s; bipe curto a cada botão; LED da Pico W piscando a cada 2 s.

## 4. Integração no firmware principal

- Inclua `board_pins.h` e remova números de GPIO literais do código existente.
- Adicione `src/hal/*.c` e `pico_generate_pio_header(<alvo> src/hal/ws2812.pio)` ao alvo atual.
- Mantenha `pico_enable_stdio_uart(<alvo> 0)`; a trava em `board_resources.h` quebra o build se isso mudar.
- Firmware com rede: troque `pico_cyw43_arch_none` por `pico_cyw43_arch_lwip_sys_freertos` e chame `cyw43_arch_init()` dentro de uma tarefa, antes de `periph_init_all()`.
