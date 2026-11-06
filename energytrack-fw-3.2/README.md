# EnergyTrack — firmware do ESP32 (WPS 3.1 e 3.2)

ESP32 DevKit (ESP32-WROOM-32) + sensor PZEM-004T v3 na UART2 (GPIO17/GPIO16)
+ sensor de temperatura LM35 no ADC1 (GPIO34), com o FreeRTOS do ESP-IDF,
compilado pelo PlatformIO. Todas as tarefas do firmware rodam no núcleo 1.

> Até 05/11/2026 o firmware era da Raspberry Pi Pico W (pico-sdk). Essa versão
> está preservada na branch `pico-w`; esta, do ESP32, vive na branch `esp-32`.

| Ambiente (PlatformIO) | WPS | O que faz |
|---|---|---|
| `energytrack_fw` | 3.1 | Tarefas FreeRTOS de aquisição (UART, ADC, timer, DMA) e processamento local de potência, energia, meta diária e anomalias, com monitor de deadline |
| `energytrack_selftest` | 3.2 | Self-test do PZEM, do LED e do log serial (`>>> SELFTEST PASS <<<`) |

Diagrama de tarefas, prioridades, deadlines e a análise de tempo de resposta:
[docs/diagrama-tarefas.md](docs/diagrama-tarefas.md). Explicação em linguagem
simples: [docs/como-funciona.md](docs/como-funciona.md).

## Estrutura

```
platformio.ini              dois ambientes (energytrack_fw e energytrack_selftest), ESP-IDF 6.1
CMakeLists.txt              projeto ESP-IDF
sdkconfig.defaults          240 MHz, tick de 1 kHz, console na UART0
include/board_pins.h        mapa de pinos (fonte única; sem dependência do SDK)
include/board_resources.h   UART2/console/ADC do ESP-IDF + trava contra console na UART do PZEM
include/energy_config.h     metas e limiares (tensão 220/127 V, meta, limite...)
src/CMakeLists.txt          componente principal: escolhe as fontes de cada firmware (ET_APP)
src/hal/                    board_init (console, UART2, LED), pzem_modbus/pzem_link,
                            acq_timer (gptimer de 1 s), adc_dma (ADC contínuo + DMA),
                            adc_frame (C puro: quadro TYPE1 e média 20:1)
src/proc/                   C puro: temp_sensor (LM35), energy, goal, anomaly, processing
src/rt/                     C puro: rt_tasks (tabela), rt_monitor (deadline), rt_sched (RTA, só nos testes)
app/energytrack_main.c      firmware do WPS 3.1
app/selftest_main.c         self-test do WPS 3.2
test/host/*.c               testes em C para o PC (saída TAP)
test/spec/*.spec.test.mjs   ponte node:test <-> critérios de aceite do onp-spec
test/bench/                 log serial da prova de bancada
tools/capturar-serial.ps1   captura o log do DevKit pela ponte USB-serial
```

## Ligação

| Sinal | ESP32 DevKit | Observação |
|---|---|---|
| PZEM RX | GPIO17 (UART2 TX) | via conversor de nível 5 V ↔ 3,3 V |
| PZEM TX | GPIO16 (UART2 RX) | o ESP32 não tolera 5 V |
| PZEM 5V | VIN / 5V | só existe com a placa no USB |
| PZEM GND | GND | terra comum |
| LM35 +Vs | VIN / 5V | o LM35 precisa de pelo menos 4 V |
| LM35 Vout | GPIO34 (ADC1 canal 6) | 10 mV/°C, nunca passa de 1,5 V: vai direto |
| LM35 GND | GND | |
| LED | GPIO2 | LED azul da própria placa |

Não use GPIO6 a GPIO11 (flash), GPIO1/GPIO3 (console) nem GPIO0/GPIO12 (boot).

## 1. Testes no PC (sem placa)

Precisa de `gcc` e `node` (≥ 20). Na raiz do repositório:

```bash
node --test "energytrack-fw-3.2/test/spec/*.spec.test.mjs"
```

Cada critério de aceite da especificação
(`.spec/features/aquisicao-processamento/spec.md`) tem um teste com
`@spec:AC-xxx` no título. Os testes em C cobrem mapa de pinos, energia, meta,
anomalias, quadro do ADC e conversão do LM35, LED, monitor de deadline e
análise de tempo de resposta. O teste de compilação (AC-020) chama o
PlatformIO (`~/.platformio`). O teste de bancada (AC-018) só passa depois que
o log do DevKit for capturado.

Gate do onp-spec (prova por critério + auditoria):

```bash
node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs verify aquisicao-processamento
node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs audit --ci
```

## 2. Compilar e gravar

Com a extensão **PlatformIO IDE** do VS Code, abra a pasta `energytrack-fw-3.2`
e use **Build** / **Upload** / **Monitor** no ambiente desejado. No terminal
(na pasta `energytrack-fw-3.2`):

```bash
pio run                                   # compila os dois ambientes
pio run -e energytrack_fw -t upload       # grava o firmware do WPS 3.1
pio device monitor                        # console a 115200 bps
```

No Windows, se `pio` não estiver no PATH, use
`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. O primeiro build baixa e
compila o ESP-IDF (alguns minutos); os seguintes só recompilam o projeto.
Saem `.pio/build/energytrack_fw/firmware.bin` e
`.pio/build/energytrack_selftest/firmware.bin`. Se a gravação não começar,
segure o botão BOOT ao conectar.

## 3. Bancada do WPS 3.1 (tempo real sem estouro de deadline)

1. Grave `energytrack_fw` (seção 2).
2. PZEM ligado com carga e LM35 no GPIO34.
3. `powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1` (11 min).
4. Rode o `verify` do onp-spec (seção 1).

O relatório sai pelo console a cada 2 s:

```
[  602s] PZEM ok=601 falha=0 | V=221.3 V I=1.234 A P=271.9 W f=60.0 Hz FP=0.98 | LM35 31.2 C
  energia: dia 1 (0h10m) dia=45.312 Wh | anterior=0.000 Wh | total=45.312 Wh | Pmed=271.5 W | lacunas recuperadas=0
  meta: uso=0% projecao=6516.000 Wh estado=OK | limite de potencia: ok
  anomalias: nenhuma | saltos=0
  rt t=602s proc:jobs=6612,miss=0,rmax=310us,d=50000us adc:jobs=6010,miss=0,rmax=140us,d=100000us pzem:jobs=601,...
  heap livre=... B | adc perdidas=0 | pilha livre (bytes): proc=... adc=... pzem=... status=... report=...
  >>> RT PASS <<< 600 s sem estouro de deadline
```

LED da placa: aceso fixo = normal; pisca a 1 Hz = PZEM sem resposta; pisca a
5 Hz = anomalia, meta estourada ou limite de potência.

## 4. Bancada do WPS 3.2 (self-test)

Grave `energytrack_selftest` (`pio run -e energytrack_selftest -t upload`),
abra o monitor serial e deixe rodar 60 s com carga. Aceite com
`>>> SELFTEST PASS <<<` (no máximo 5% de leituras com falha). LED aceso fixo =
PZEM respondendo; piscando = sem resposta válida.

## Ajustes

Metas e limiares ficam em `include/energy_config.h`: tensão nominal
(`CFG_NOMINAL_VOLTAGE` 220 ou 127, faixas do PRODIST), meta diária (5 kWh),
limite de potência (3000 W por 10 s), frequência, fator de potência e
temperatura. Prioridades, períodos e deadlines ficam em `src/rt/rt_tasks.c` —
mudou lá, atualize `docs/diagrama-tarefas.md` (o teste AC-021 confere).
Pinos, só em `include/board_pins.h` (o teste AC-019 confere as regras do ESP32).
