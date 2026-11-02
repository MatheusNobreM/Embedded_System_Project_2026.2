# EnergyTrack — firmware da Pico W (WPS 3.1 e 3.2)

Raspberry Pi Pico W (RP2040) + sensor PZEM-004T v3 na UART0 (GP0/GP1), com
FreeRTOS. Sem BitDogLab.

| Alvo | WPS | O que faz |
|---|---|---|
| `energytrack_fw` | 3.1 | Tarefas FreeRTOS de aquisição (UART, ADC, timer, DMA) e processamento local de potência, energia, meta diária e anomalias, com monitor de deadline |
| `energytrack_selftest` | 3.2 | Self-test do PZEM, do LED e do log USB (`>>> SELFTEST PASS <<<`) |

Diagrama de tarefas, prioridades, deadlines e a análise de tempo de resposta:
[docs/diagrama-tarefas.md](docs/diagrama-tarefas.md).

## Estrutura

```
include/board_pins.h        mapa de pinos (fonte única; sem dependência do SDK)
include/board_resources.h   uart0 + trava contra printf na UART0
include/energy_config.h     metas e limiares (tensão 220/127 V, meta, limite...)
config/FreeRTOSConfig.h     RP2040, núcleo único
src/hal/                    board_init (UART0, CYW43, LED), pzem_modbus/pzem_link,
                            acq_timer (alarme de 1 s), adc_dma (ADC4 + DMA ping-pong)
src/proc/                   C puro: temp_sensor, energy, goal, anomaly, processing
src/rt/                     C puro: rt_tasks (tabela), rt_monitor (deadline), rt_sched (RTA, só nos testes)
app/energytrack_main.c      firmware do WPS 3.1
app/selftest_main.c         self-test do WPS 3.2
test/host/*.c               testes em C para o PC (saída TAP)
test/spec/*.spec.test.mjs   ponte node:test <-> critérios de aceite do onp-spec
test/bench/                 log serial da prova de bancada
tools/capturar-serial.ps1   captura o log da Pico W pela USB
```

## Ligação

| PZEM | Pico W | Observação |
|---|---|---|
| RX | pino 1 (GP0, UART0 TX) | via conversor de nível 5 V ↔ 3,3 V |
| TX | pino 2 (GP1, UART0 RX) | GP1 não tolera 5 V |
| GND | pino 3 | terra comum |
| 5V | pino 40 (VBUS) | só existe com a Pico no USB |

## 1. Testes no PC (sem placa)

Precisa de `gcc` e `node` (≥ 20). Na raiz do repositório:

```bash
node --test "energytrack-fw-3.2/test/spec/*.spec.test.mjs"
```

Cada critério de aceite da especificação
(`.spec/features/aquisicao-processamento/spec.md`) tem um teste com
`@spec:AC-xxx` no título. Os testes em C cobrem energia, meta, anomalias,
conversão de temperatura, LED, monitor de deadline e análise de tempo de
resposta. O teste de compilação (AC-020) usa as ferramentas da extensão em
`~/.pico-sdk`. O teste de bancada (AC-018) só passa depois que o log da Pico W
for capturado.

Gate do onp-spec (prova por critério + auditoria):

```bash
node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs verify aquisicao-processamento
node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs audit --ci
```

## 2. Compilar

Com a extensão **Raspberry Pi Pico** do VS Code (SDK 2.1.1) e o FreeRTOS-Kernel
em `~/pico/FreeRTOS-Kernel` (ou `-DFREERTOS_KERNEL_PATH=...`):

- pela extensão: **Compile Project**; ou
- no terminal do VS Code (a extensão já põe as ferramentas no PATH):

  ```bash
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```

Saem `build/energytrack_fw.uf2` e `build/energytrack_selftest.uf2`.

## 3. Bancada do WPS 3.1 (tempo real sem estouro de deadline)

1. Grave `energytrack_fw.uf2` (segure BOOTSEL ao ligar o USB e arraste o arquivo).
2. PZEM ligado com carga.
3. `powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1` (11 min).
4. Rode o `verify` do onp-spec (seção 1).

O relatório sai pela USB a cada 2 s:

```
[  602s] PZEM ok=601 falha=0 | V=221.3 V I=1.234 A P=271.9 W f=60.0 Hz FP=0.98 | placa 31.2 C
  energia: dia 1 (0h10m) dia=45.312 Wh | anterior=0.000 Wh | total=45.312 Wh | Pmed=271.5 W | lacunas recuperadas=0
  meta: uso=0% projecao=6516.000 Wh estado=OK | limite de potencia: ok
  anomalias: nenhuma | saltos=0
  rt t=602s proc:jobs=6612,miss=0,rmax=310us,d=50000us adc:jobs=6010,miss=0,rmax=140us,d=100000us pzem:jobs=601,...
  heap livre=... | pilha livre (palavras): proc=... adc=... pzem=... status=... report=...
  >>> RT PASS <<< 600 s sem estouro de deadline
```

LED da placa: aceso fixo = normal; pisca a 1 Hz = PZEM sem resposta; pisca a
5 Hz = anomalia, meta estourada ou limite de potência.

## 4. Bancada do WPS 3.2 (self-test)

Grave `energytrack_selftest.uf2`, abra o monitor serial e deixe rodar 60 s com
carga. Aceite com `>>> SELFTEST PASS <<<` (no máximo 5% de leituras com falha).
LED aceso fixo = PZEM respondendo; piscando = sem resposta válida.

## Ajustes

Metas e limiares ficam em `include/energy_config.h`: tensão nominal
(`CFG_NOMINAL_VOLTAGE` 220 ou 127, faixas do PRODIST), meta diária (5 kWh),
limite de potência (3000 W por 10 s), frequência, fator de potência e
temperatura. Prioridades, períodos e deadlines ficam em `src/rt/rt_tasks.c` —
mudou lá, atualize `docs/diagrama-tarefas.md` (o teste AC-021 confere).
