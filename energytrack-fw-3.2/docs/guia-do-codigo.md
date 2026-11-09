# Guia do código: pasta por pasta, função por função

Este guia explica **cada pasta, cada arquivo e cada função** do firmware em
`energytrack-fw-3.2/`, e como elas se encaixam no fluxo. Foi escrito para quem
sabe C mas está chegando agora no projeto.

- Quer só a visão geral? Leia [como-funciona.md](como-funciona.md).
- Quer prioridades, deadlines e a prova de tempo real? Leia
  [diagrama-tarefas.md](diagrama-tarefas.md).
- Quer compilar, gravar e testar? Leia o [README](../README.md).

---

## Sumário

1. [O que o firmware faz, em 5 linhas](#1-o-que-o-firmware-faz-em-5-linhas)
2. [As pastas e por que elas são separadas](#2-as-pastas-e-por-que-elas-são-separadas)
3. [Como o build escolhe os arquivos](#3-como-o-build-escolhe-os-arquivos)
4. [O fluxo completo, do boot ao relatório](#4-o-fluxo-completo-do-boot-ao-relatório)
5. [`include/`: as configurações](#5-include-as-configurações)
6. [`src/hal/`: a conversa com o hardware](#6-srchal-a-conversa-com-o-hardware)
7. [`src/proc/`: as contas](#7-srcproc-as-contas)
8. [`src/rt/`: o tempo real](#8-srcrt-o-tempo-real)
9. [`app/`: os dois programas](#9-app-os-dois-programas)
10. [Quem chama quem](#10-quem-chama-quem)
11. [Como as tarefas conversam sem brigar](#11-como-as-tarefas-conversam-sem-brigar)
12. [Testes e ferramentas](#12-testes-e-ferramentas)
13. [Por onde começar a ler](#13-por-onde-começar-a-ler)

---

## 1. O que o firmware faz, em 5 linhas

1. Um **timer de hardware** do ESP32 dispara uma vez por segundo.
2. Cada disparo acorda a tarefa que **pergunta ao medidor PZEM-004T** (pela
   UART2, protocolo Modbus) a tensão, a corrente, a potência e o resto.
3. A leitura vai para a tarefa que **faz as contas**: energia do dia, meta de
   consumo, limite de potência e anomalias da rede.
4. O resultado vira uma **"fotografia"** do estado, que duas tarefas de saída
   leem: uma controla o **LED** da placa, a outra **imprime um relatório** na
   serial a cada 2 s.
5. Cada etapa tem o tempo cronometrado. Se alguma passar do prazo (deadline),
   o relatório acusa.

---

## 2. As pastas e por que elas são separadas

```
energytrack-fw-3.2/
├── include/        configurações: pinos, recursos do ESP32, metas e limites
├── src/
│   ├── hal/        conversa com o hardware: UART, LED, timer, protocolo do PZEM
│   ├── proc/       contas: energia, meta, anomalias (C puro)
│   └── rt/         tempo real: tabela de tarefas, cronômetro, análise (C puro)
├── app/            os programas: cria as tarefas e liga tudo (FreeRTOS)
├── test/           testes no PC (host), ponte com a spec e log da bancada
├── tools/          script que grava o log serial da placa
└── docs/           esta documentação
```

### O que cada nome quer dizer

| Pasta | Nome por extenso | Comparação | Em uma frase |
|---|---|---|---|
| `include/` | arquivos de cabeçalho globais | a **ficha técnica** | Números que todo mundo consulta: qual pino é o quê, qual a meta, quais os limites. Não tem lógica. |
| `src/hal/` | *Hardware Abstraction Layer* (camada de abstração de hardware) | as **mãos e os ouvidos** | O único lugar que mexe direto em periférico do ESP32 (UART, GPIO, timer). Esconde os detalhes do ESP-IDF atrás de funções simples como `pzem_link_read()`. |
| `src/proc/` | processamento | a **calculadora** | Recebe números e o horário e devolve resultados. Não sabe que existe ESP32, FreeRTOS ou UART. |
| `src/rt/` | *real time* (tempo real) | o **cronômetro e o regulamento** | Diz quais tarefas existem e quais os prazos, mede se os prazos foram cumpridos e prova no papel que eles cabem. |
| `app/` | aplicação | o **maestro** | Cria as tarefas e as filas do FreeRTOS e chama `hal`, `proc` e `rt` na ordem certa. |

### A regra que justifica a separação

```
                 app/  (FreeRTOS + ESP-IDF)
               ┌──┴───────────┬──────────────┐
               ▼              ▼              ▼
          src/hal/        src/proc/       src/rt/
       (precisa do       (C puro)        (C puro)
         ESP-IDF)            │
               │             │ usa só o tipo pzem_measurement_t
               └──► pzem_modbus.h ◄┘   (também C puro)

          include/  ← todos podem ler
```

- **`proc/` e `rt/` não incluem nada do ESP-IDF nem do FreeRTOS.** O horário
  chega como parâmetro (`uint64_t t_us`), em vez de a função perguntar ao
  relógio. Por isso o mesmo código compila com o `gcc` do PC e é testado lá,
  sem placa (pasta `test/host/`).
- Dentro de `hal/`, o `pzem_modbus.c` também é C puro (só monta e confere
  bytes), então é testável no PC. Os outros arquivos de `hal/` chamam o ESP-IDF
  e só rodam na placa.
- Só `app/` conhece as tarefas e as filas. Se um dia o FreeRTOS for trocado,
  `proc/` e `rt/` não mudam.

### Convenções de nome que aparecem em todo lugar

| Convenção | Significado | Exemplo |
|---|---|---|
| Sufixo de unidade | A unidade do número está no nome | `voltage_dv` (decivolts), `current_ma` (miliampères), `power_dw` (deciwatts), `energy_wh`, `day_mwh` (miliwatt-hora), `t_us` (microssegundos), `freq_dhz` (decihertz), `pf_centi` (centésimos) |
| `s_` | Variável global `static` do arquivo | `s_q_samples`, `s_proc` |
| `BOARD_` | Vem de `board_pins.h` / `board_resources.h` | `BOARD_PIN_LED` |
| `CFG_` | Vem de `energy_config.h` | `CFG_DAILY_GOAL_MWH` |
| `RT_` | Vem de `rt_tasks.h` | `RT_TASK_PZEM` |
| `*_state_t` | O "estado" (memória) de um módulo | `energy_state_t` |
| `*_config_t` | Os parâmetros de um módulo | `goal_config_t` |
| `*_init()` | Zera o estado e guarda a configuração | `anomaly_init()` |

**Por que tudo em inteiros?** O código nasceu na Raspberry Pi Pico W, cujo
processador não tem ponto flutuante. No ESP32 isso não é mais obrigatório, mas
inteiros dão o mesmo resultado bit a bit no PC e na placa, sem arredondamento
escondido. Então `2205` em `voltage_dv` quer dizer 220,5 V, e `10000` em
`power_dw` quer dizer 1000,0 W.

---

## 3. Como o build escolhe os arquivos

O mesmo repositório gera **dois firmwares diferentes**. Quem decide é a
variável `ET_APP`:

```
platformio.ini                       src/CMakeLists.txt
───────────────                      ──────────────────
[env:energytrack_fw]        ──►  ET_APP=energytrack  ──►  energytrack_main.c + hal + proc + rt
  -DET_APP=energytrack
[env:energytrack_selftest]  ──►  ET_APP=selftest     ──►  selftest_main.c + parte do hal
  -DET_APP=selftest
```

| Arquivo | `energytrack_fw` (WPS 3.1) | `energytrack_selftest` (WPS 3.2) | Testes no PC |
|---|:-:|:-:|:-:|
| `app/energytrack_main.c` | ✔ | | |
| `app/selftest_main.c` | | ✔ | |
| `hal/board_init.c` | ✔ | ✔ | |
| `hal/pzem_link.c` | ✔ | ✔ | |
| `hal/pzem_modbus.c` | ✔ | ✔ | ✔ |
| `hal/acq_timer.c` | ✔ | | |
| `proc/*.c` | ✔ | | ✔ |
| `rt/rt_monitor.c`, `rt/rt_tasks.c` | ✔ | | ✔ |
| `rt/rt_sched.c` | | | ✔ (só no PC) |

Outros arquivos do build:

| Arquivo | Para que serve |
|---|---|
| `platformio.ini` | Fixa a plataforma (`espressif32@8.0.0`, ESP-IDF 6.1), a placa (`esp32dev`), a velocidade do monitor (115200) e cria os dois ambientes. `monitor_rts = 0` e `monitor_dtr = 0` evitam que o monitor segure a placa em reset. |
| `CMakeLists.txt` (raiz) | Receita padrão de projeto ESP-IDF. Só diz "o projeto se chama `energytrack_fw`". |
| `src/CMakeLists.txt` | Escolhe as fontes conforme `ET_APP`, diz onde ficam os headers (`../include hal proc rt`), quais componentes do ESP-IDF são usados (UART, GPIO, gptimer, esp_timer, FreeRTOS) e liga `-Wall -Wextra` só no nosso código. |
| `sdkconfig.defaults` | Configuração do ESP-IDF: CPU a 240 MHz, tick do FreeRTOS de 1 ms (1000 Hz), detecção de estouro de pilha, console na UART0 a 115200 bps, watchdog olhando os dois núcleos, flash de 4 MB. |

---

## 4. O fluxo completo, do boot ao relatório

### 4.1 O boot (acontece uma vez)

```
ESP-IDF liga o FreeRTOS
        │
        ▼
app_main()                    roda no núcleo 0
  └─ cria init_task           prioridade 10, presa no núcleo 1
        │
        ▼
init_task()                   roda no núcleo 1, antes de qualquer outra tarefa
  1. board_init_console()     printf passa a usar o driver da UART0
  2. board_init_led()         GPIO2 como saída, LED apagado
  3. board_init_pzem_uart()   UART2 a 9600 bps nos GPIO17/16
  4. cria as 3 filas          s_q_samples (8), s_q_snapshot (1), s_q_pzem_release (1)
  5. rt_monitor_init() x4     um cronômetro por tarefa, com o deadline de rt_tasks.c
  6. cria as 4 tarefas        proc, pzem, status, report (prioridades 5, 3, 2, 1)
  7. acq_timer_start()        liga o timer de 1 s — só agora, com tudo pronto
  8. vTaskDelete(NULL)        a init se apaga
```

Por que essa volta toda em vez de fazer tudo em `app_main()`? Porque o ESP-IDF
coloca a interrupção de cada driver **no núcleo de quem instalou o driver**.
Instalando tudo de dentro de uma tarefa presa no núcleo 1, as interrupções da
UART, do console e do timer também ficam no núcleo 1, junto com as tarefas.
Assim a análise de tempo real vale para um núcleo só.

E por que prioridade 10? Porque é maior que a de todas as tarefas que ela cria
(1 a 5). Enquanto a `init` não se apaga, nenhuma delas roda, então nenhuma
tarefa vê uma fila ainda não criada.

### 4.2 Um ciclo de medição (a cada 1 segundo)

```
tempo   quem                 o que acontece
─────   ──────────────────   ─────────────────────────────────────────────────────
t0      timer (ISR)          on_alarm() → on_pzem_release(t0): põe t0 na fila
                             s_q_pzem_release e sai.
t0+     pzem_task            acorda com t0. pzem_link_read():
                               limpa a UART2, manda 8 bytes, dorme no driver
                               enquanto chegam os 25 bytes da resposta,
                               confere CRC e cabeçalho, separa as grandezas.
t1      pzem_task            send_sample(): carimba t1 e põe a amostra em
                             s_q_samples. rt_monitor_job_done(pzem, t0, agora).
t1+     proc_task            acorda (tem prioridade maior, toma a CPU na hora).
         (prio 5)            proc_on_pzem(): energia, meta, anomalias.
                             proc_snapshot(): monta a fotografia.
                             xQueueOverwrite(s_q_snapshot): troca a foto antiga.
                             rt_monitor_job_done(proc, t1, agora).
...     pzem e proc          voltam a dormir até o próximo t0 (t0 + 1 s).
```

Prazos: a `pzem` tem 500 ms de t0 até terminar; a `proc` tem 50 ms de t1 até
terminar. Na prática a `pzem` leva algumas dezenas de milissegundos (quase tudo
dormindo, esperando a serial) e a `proc` leva menos de 1 ms.

### 4.3 A saída (em paralelo, no próprio ritmo)

```
a cada 50 ms   status_task   espia a foto (xQueuePeek) → proc_led_pattern()
                             → led_pattern_level() → board_led_set()
a cada 2 s     report_task   espia a foto → print_* → linha "rt t=..."
                             → decide se já pode dizer ">>> RT PASS <<<"
```

`xQueuePeek` **lê sem retirar** a foto da fila. Por isso as duas tarefas de
saída podem ler a mesma foto quantas vezes quiserem, e a `proc` só a troca
quando chega uma leitura nova.

---

## 5. `include/`: as configurações

São três headers usados pelo projeto inteiro. Nenhum deles tem função, só
constantes.

### `board_pins.h`: o mapa de pinos

É a **única fonte de números de GPIO**. Nenhum outro arquivo pode escrever um
número de pino direto. Este header não inclui nada do ESP-IDF de propósito,
para o teste `test/host/test_pinmap.c` conseguir conferi-lo no PC.

| Macro | Valor | Significado |
|---|---|---|
| `BOARD_PZEM_UART_INDEX` | 2 | O PZEM usa a UART2 |
| `BOARD_PIN_PZEM_TX` | 17 | GPIO17 manda dados para o RX do PZEM |
| `BOARD_PIN_PZEM_RX` | 16 | GPIO16 recebe dados do TX do PZEM |
| `BOARD_PZEM_BAUD` | 9600 | Velocidade do Modbus do PZEM |
| `BOARD_PZEM_MODBUS_ADDR` | `0xF8` | Endereço "geral": responde qualquer PZEM sozinho no fio |
| `BOARD_PIN_LED` | 2 | LED azul da própria placa |
| `BOARD_LED_ON_LEVEL` | 1 | O LED acende com nível alto |
| `BOARD_CONSOLE_UART_INDEX` | 0 | Console (printf) na UART0, que vai para o USB |
| `BOARD_PIN_CONSOLE_TX/RX` | 1 / 3 | Pinos do console |
| `BOARD_CONSOLE_BAUD` | 115200 | Velocidade do console |
| `BOARD_PIN_FLASH_*` | 6 a 11 | Ligados à memória flash: **não usar** |
| `BOARD_PIN_BOOT` | 0 | Botão BOOT: em 0 no reset entra no modo de gravação |
| `BOARD_PIN_MTDI` | 12 | Em 1 no reset muda a tensão da flash: **não usar** |
| `BOARD_ASSIGNED_PINS` | lista | Pinos que o projeto usa (o teste confere que não colidem) |
| `BOARD_RESERVED_PINS` | lista | Pinos proibidos (o teste confere que nenhum usado está aqui) |

### `board_resources.h`: o mapa traduzido para o ESP-IDF

O ESP-IDF não aceita `int` para porta de UART e pino: ele quer os tipos
`uart_port_t` e `gpio_num_t`. Este header faz essa conversão
(`BOARD_PZEM_UART`, `BOARD_CONSOLE_UART`, `BOARD_GPIO_LED`, ...).

Ele também tem **duas travas de compilação**:

- `_Static_assert(...)`: impede que alguém coloque o PZEM na mesma UART do
  console.
- `#error` se o `sdkconfig` mandar o console do ESP-IDF para a UART do PZEM.
  Se isso acontecesse, todo `printf` iria parar no fio do PZEM e estragaria os
  quadros Modbus.

### `energy_config.h`: metas e limites

Todos os números que definem "o que é normal". Mudou aqui, muda o
comportamento inteiro. Quem lê esses valores é só `proc_default_config()` (em
`processing.c`).

| Macro | Valor | Usado em | Significado |
|---|---|---|---|
| `CFG_NOMINAL_VOLTAGE` | 220 | escolhe as faixas abaixo | Rede de 220 V ou 127 V |
| `CFG_V_ADEQUATE_MIN/MAX_DV` | 2020 / 2310 | anomalias | Tensão adequada: 202 V a 231 V (PRODIST, ANEEL) |
| `CFG_V_PRECARIOUS_MIN/MAX_DV` | 1910 / 2330 | anomalias | Fora da adequada mas dentro disto é "precária"; fora disto, "crítica" |
| `CFG_FREQ_MIN/MAX_DHZ` | 595 / 605 | anomalias | Frequência aceitável: 59,5 Hz a 60,5 Hz |
| `CFG_PF_MIN_CENTI` | 92 | anomalias | Fator de potência mínimo: 0,92 |
| `CFG_PF_MIN_POWER_DW` | 1000 | anomalias | Só avalia o fator de potência com 100 W ou mais |
| `CFG_ALERT_PERSIST` | 3 | anomalias | Leituras seguidas para ligar (ou desligar) um alerta |
| `CFG_SENSOR_FAIL_LIMIT` | 5 | anomalias | Falhas seguidas para dizer "sensor sem resposta" |
| `CFG_SPIKE_WARMUP` | 30 | anomalias | Leituras para aprender a "potência normal" antes de detectar saltos |
| `CFG_SPIKE_K` | 4 | anomalias | Salto precisa ser 4× maior que a variação típica... |
| `CFG_SPIKE_MIN_STEP_DW` | 10000 | anomalias | ...e pelo menos 1000 W |
| `CFG_DAY_LENGTH_S` | 86400 | energia | Um "dia" dura 24 h, contado do boot (não há relógio de parede) |
| `CFG_ENERGY_MAX_GAP_S` | 3 | energia | Buraco maior que 3 s entre leituras usa o contador do PZEM |
| `CFG_DAILY_GOAL_MWH` | 5000000 | meta | Meta: 5 kWh por dia |
| `CFG_GOAL_WARN_PCT` | 80 | meta | "ATENCAO" a partir de 80% da meta |
| `CFG_GOAL_MIN_ELAPSED_S` | 3600 | meta | Projeção só depois de 1 h de dia |
| `CFG_POWER_LIMIT_DW` | 30000 | limite | Limite de potência: 3000 W... |
| `CFG_POWER_HOLD_S` | 10 | limite | ...mantido por 10 s seguidos |
| `CFG_POWER_CLEAR_PCT` | 95 | limite | O alerta só some abaixo de 95% do limite (2850 W) |

---

## 6. `src/hal/`: a conversa com o hardware

Quatro arquivos, cada um com um `.c` (o código) e um `.h` (o que os outros
podem chamar).

### 6.1 `board_init.c`: liga os periféricos

Cada função inicializa **um** recurso e não cria tarefas. Se algo falhar,
`ESP_ERROR_CHECK` reinicia a placa: sem esses recursos não há o que fazer.

#### `void board_init_console(void)`

- **Faz:** instala o driver da UART0 (buffer de recepção de 256 B e de
  transmissão de 2048 B) e manda o `printf` usar esse driver
  (`uart_vfs_dev_use_driver`).
- **Para que serve:** sem isso, o `printf` escreve na serial byte a byte,
  **esperando** cada um sair (espera ocupada). A 115200 bps, 1 KB de relatório
  levaria quase 90 ms com a CPU travada. Com o driver, o `printf` só copia o
  texto para o buffer de 2 KB e volta; uma interrupção vai esvaziando o buffer
  sozinha.
- **Detalhe:** o `fflush(stdout)` antes garante que o que já estava pendente
  saia antes da troca.

#### `void board_init_pzem_uart(void)`

- **Faz:** configura a UART2 em 9600 bps, 8 bits, sem paridade, 1 bit de parada
  (o "8N1" do PZEM), nos pinos GPIO17 (TX) e GPIO16 (RX).
- **Detalhes que importam:**
  - Buffer de recepção de 256 B e **nenhum** de transmissão: o pedido tem só
    8 bytes e cabe direto na FIFO de hardware (128 B), então o envio é
    instantâneo.
  - `uart_set_rx_timeout(..., 3)`: o driver entrega os bytes recebidos depois
    de 3 "caracteres" de silêncio (cerca de 3 ms a 9600 bps) em vez dos 10 do
    padrão. Assim a tarefa recebe o fim da resposta mais cedo.
  - Pull-up no RX: se o conversor de nível estiver desligado, a linha fica em
    nível alto (repouso) em vez de flutuar e gerar lixo.

#### `void board_init_led(void)`

- **Faz:** coloca o GPIO2 como saída e apaga o LED.

#### `void board_led_set(bool on)`

- **Faz:** acende (`true`) ou apaga (`false`) o LED. Usa
  `BOARD_LED_ON_LEVEL` para saber se "aceso" é nível 1 ou 0, então trocar
  para um LED invertido é só mudar o `board_pins.h`.
- **Quem chama:** só a `status_task` (ela é a dona do LED).

### 6.2 `acq_timer.c`: o despertador de 1 segundo

#### `bool acq_timer_start(uint32_t period_us, acq_timer_cb on_release)`

- **Recebe:** o período (1 000 000 µs) e a função que deve ser chamada a cada
  disparo.
- **Faz:** cria um *gptimer* (um contador de hardware do ESP32) contando a
  1 MHz (1 tique = 1 µs), com alarme em `period_us` e **recarga automática**:
  quando o alarme toca, o próprio hardware zera o contador e recomeça.
- **Devolve:** `false` se não houver timer livre.
- **Por que um timer de hardware e não `vTaskDelay(1000)`?** Duas razões:
  1. **Sem deriva.** Com `vTaskDelay`, o atraso de cada ciclo se soma ao
     próximo. Com a recarga feita pelo hardware, o 1000º disparo acontece
     exatamente em 1000 s.
  2. **Horário exato da liberação.** A interrupção anota o instante em que o
     trabalho foi liberado, e esse é o ponto de partida do cronômetro do
     deadline.

#### `static bool on_alarm(...)` (interna)

- É a **rotina de interrupção** (ISR) que o driver chama quando o alarme toca.
- Só lê o relógio (`esp_timer_get_time()`, o mesmo relógio que as tarefas usam)
  e repassa para a função registrada (`on_pzem_release` no firmware).
- Devolve o que essa função devolver: `true` significa "acordei uma tarefa
  mais importante do que a que estava rodando", e o driver troca de tarefa na
  saída da interrupção.

### 6.3 `pzem_modbus.c`: o protocolo (C puro)

Só monta e confere bytes. Não sabe o que é UART. Por isso é testado no PC.

**Como é a conversa com o PZEM** (Modbus-RTU, função 0x04 "ler registradores
de entrada"):

```
Pedido (8 bytes) — o ESP32 manda:
  F8   04   00 00   00 0A   64 64
  │    │    └─┬─┘   └─┬─┘   └─┬─┘
  │    │      │       │       └ CRC-16 (byte baixo primeiro)
  │    │      │       └ quantidade = 10 registradores
  │    │      └ registrador inicial = 0x0000
  │    └ função 0x04 = ler registradores de entrada
  └ endereço do PZEM (0xF8 = "geral")

Resposta normal (25 bytes) — o PZEM devolve:
  F8  04  14  [ 20 bytes = 10 registradores de 16 bits ]  CRC_lo CRC_hi
              reg0     tensão          (0,1 V)
              reg1+2   corrente        (0,001 A, 32 bits: reg1 = parte baixa)
              reg3+4   potência        (0,1 W,   32 bits)
              reg5+6   energia         (1 Wh,    32 bits)
              reg7     frequência      (0,1 Hz)
              reg8     fator potência  (0,01)
              reg9     alarme          (0xFFFF = ativo)

Resposta de erro (5 bytes):
  F8  84  código  CRC_lo CRC_hi      (84 = 04 | 0x80 → "exceção")
```

#### `uint16_t pzem_crc16(const uint8_t *data, size_t len)`

- **Faz:** calcula o CRC-16/MODBUS dos bytes: começa em `0xFFFF` e, para cada
  bit, desloca e aplica o polinômio `0xA001`.
- **Para que serve:** é a "assinatura" do quadro. Se um único bit mudar por
  ruído no fio, o CRC calculado não bate com o recebido e o quadro é
  descartado.

#### `void pzem_build_read_request(uint8_t addr, uint8_t out[8])`

- **Faz:** preenche os 8 bytes do pedido mostrado acima, já com o CRC no fim.

#### `pzem_parse_result_t pzem_parse_response(addr, frame, len, out)`

- **Recebe:** o endereço esperado, os bytes recebidos, quantos chegaram e onde
  guardar o resultado.
- **Confere, nesta ordem:**
  1. É uma **resposta de exceção** (5 bytes com função `0x84`)? Se o CRC dela
     bater → `PZEM_PARSE_EXCEPTION`; senão → `PZEM_PARSE_BAD_CRC`.
  2. Chegaram menos de 25 bytes? → `PZEM_PARSE_SHORT` (o PZEM não respondeu a
     tempo ou houve ruído).
  3. O CRC dos 23 primeiros bytes bate com os 2 últimos? Senão →
     `PZEM_PARSE_BAD_CRC`.
  4. Endereço, função e contagem (20 bytes) são os esperados? Senão →
     `PZEM_PARSE_BAD_HEADER`.
  5. Tudo certo: separa as grandezas em `*out` → `PZEM_PARSE_OK`.
- **Garantia importante:** em qualquer erro, `*out` **não é tocado**. Quem
  chama nunca recebe meia leitura.

#### `static reg_at()` e `static reg32_at()` (internas)

- `reg_at(d, i)`: junta 2 bytes no registrador `i` (byte alto primeiro, o
  padrão Modbus).
- `reg32_at(d, i)`: junta dois registradores num valor de 32 bits. No PZEM, a
  **parte baixa vem primeiro** (`reg i`) e a alta depois (`reg i+1`).

#### Os tipos do `pzem_modbus.h`

- `pzem_measurement_t`: uma leitura completa (`voltage_dv`, `current_ma`,
  `power_dw`, `energy_wh`, `freq_dhz`, `pf_centi`, `alarm`). Esse tipo é usado
  pelo projeto inteiro, inclusive por `proc/`.
- `pzem_parse_result_t`: o resultado da conferência. Os números importam no
  self-test, que imprime `ultimo=N`: **0** OK, **1** curta (SHORT), **2** CRC
  errado, **3** cabeçalho errado, **4** exceção.

### 6.4 `pzem_link.c`: a transação pela UART

#### `pzem_parse_result_t pzem_link_read(pzem_measurement_t *out)`

É a função que o resto do código usa para "ler o PZEM". Passo a passo:

1. Monta o pedido com `pzem_build_read_request()`.
2. `uart_flush_input()`: joga fora bytes velhos que estejam no buffer
   (resto de uma resposta atrasada, ruído).
3. `uart_write_bytes()`: envia os 8 bytes.
4. **Espera a resposta dormindo**, em passos de 10 ms, até 200 ms no máximo
   (`PZEM_LINK_MAX_WAIT_MS`):
   - `uart_read_bytes(..., 10 ms)` bloqueia a tarefa dentro do FreeRTOS: a CPU
     fica livre para as outras tarefas enquanto os bytes chegam pela
     interrupção da UART.
   - Vai juntando o que chega até completar 25 bytes.
   - Se chegarem exatamente 5 bytes com função `0x84`, é uma resposta de
     exceção: sai do laço na hora, sem esperar os 200 ms.
5. Entrega tudo para `pzem_parse_response()` e devolve o resultado.

Tempo típico: os 25 bytes levam cerca de 26 ms para trafegar a 9600 bps. No
pior caso, o laço dá 21 passos de 10 ms (220 ms dormindo), número usado como
"suspensão" da tarefa `pzem` em `rt_tasks.c`.

---

## 7. `src/proc/`: as contas

Todos os arquivos seguem o mesmo padrão: uma `struct` de **config** (os
limites), uma `struct` de **estado** (a memória entre uma leitura e outra), uma
função `*_init()` e funções que recebem uma leitura e atualizam o estado.

### 7.1 `energy.c`: energia do dia e desde o boot

#### O estado (`energy_state_t`)

| Campo | Guarda |
|---|---|
| `has_prev`, `prev_t_us`, `prev_power_dw`, `prev_counter_wh` | A leitura anterior (para calcular o trapézio e a diferença do contador) |
| `day_start_us`, `day_index` | Quando o dia atual começou e qual dia é (0 = primeiro) |
| `day_units`, `total_units` | Energia do dia e desde o boot, em unidades internas (veja abaixo) |
| `prev_day_mwh` | Quanto foi gasto no dia anterior |
| `gaps_recovered` | Quantos buracos de leitura foram cobertos pelo contador do PZEM |

**A unidade interna.** O código guarda `(P1 + P2) × Δt`, em dW·µs. Isso é o
**dobro** da área do trapézio (a média seria `(P1 + P2) / 2 × Δt`). Guardar o
dobro evita dividir por 2 a cada leitura e perder o "meio" nos inteiros.
Conversão: 1 mWh = 3,6 W·s = 36 × 10⁶ dW·µs, então 1 mWh = **72 × 10⁶
unidades** (`UNITS_PER_MWH`).

Exemplo: 1000 W e, 1 s depois, 1200 W.
`(10000 + 12000) × 1 000 000 = 22 × 10⁹` unidades ÷ 72 × 10⁶ ≈ **305 mWh**
(≈ 0,306 Wh, ou 1100 J).

#### Funções públicas

| Função | O que faz |
|---|---|
| `energy_init(s, cfg, now_us)` | Zera tudo e marca o começo do dia 0 em `now_us`. |
| `energy_on_sample(s, t_us, power_dw, counter_wh)` | **A função principal.** Recebe uma leitura válida. Se a anterior foi há **até 3 s**, soma o trapézio. Se foi há **mais de 3 s** (o PZEM ficou sem responder), usa a diferença do contador interno do PZEM (`counter_wh` atual − anterior, em Wh), que continua contando mesmo sem a placa perguntar, e incrementa `gaps_recovered`. Se o contador **voltou para trás** (alguém zerou o PZEM), o intervalo fica sem energia, em vez de virar energia negativa. No fim chama `energy_tick()` e guarda esta leitura como "anterior". |
| `energy_tick(s, now_us)` | Vira o dia quantas vezes for preciso até `now_us`. É chamada também nas leituras com falha, para o dia virar mesmo com o PZEM desligado. |
| `energy_day_mwh(s)` | Energia do dia atual, em mWh. |
| `energy_total_mwh(s)` | Energia desde o boot, em mWh. |
| `energy_prev_day_mwh(s)` | Energia do dia anterior, em mWh. |
| `energy_day_elapsed_us(s, now_us)` | Quanto tempo do dia atual já passou. |
| `energy_day_avg_power_dw(s, now_us)` | Potência média do dia = energia ÷ tempo (o `2 ×` no divisor desfaz o "dobro" da unidade interna). |

#### Funções internas (`static`)

| Função | O que faz |
|---|---|
| `day_end_us(s)` | Quando o dia atual termina (`day_start_us + 24 h`). |
| `close_day(s)` | Fecha o dia: copia o total para `prev_day_mwh`, zera `day_units`, avança `day_start_us` e `day_index`. |
| `proportion(amount, part, whole)` | Calcula `amount × part ÷ whole` sem estourar 64 bits. Serve para dividir um pedaço de energia proporcionalmente ao tempo. |
| `add_spread(s, amount, scale, t0, t1)` | Soma energia "espalhada" entre `t0` e `t1`. Se o intervalo **atravessa a meia-noite** (a virada das 24 h), divide a energia proporcionalmente: o pedaço antes da virada vai para o dia que fecha, o resto para o novo. Se parte do intervalo é de um dia que já tinha sido fechado por `energy_tick`, esse pedaço vai direto para `prev_day_mwh`. |

### 7.2 `goal.c`: meta diária e limite de potência

#### `goal_status_t goal_update_energy(g, day_mwh, day_elapsed_us, day_length_us)`

- **Faz:** calcula a situação da meta e guarda em `g->status`.
  - `used_pct` = consumo do dia ÷ meta × 100.
  - `projected_mwh` = "se continuar nesse ritmo, quanto vou gastar no dia?" =
    consumo × 24 h ÷ tempo decorrido. Só calcula depois de 1 h de dia, para
    não alarmar com pouca amostra (nos primeiros minutos qualquer chaleira
    projetaria um consumo absurdo).
- **Estados, do mais grave para o menos grave** (vence o primeiro que bater):

| Estado | Condição | Exemplo (meta 5 kWh) |
|---|---|---|
| `GOAL_ESTOURADA` | consumo ≥ 100% da meta | 5,2 kWh |
| `GOAL_ATENCAO` | consumo ≥ 80% | 4,1 kWh |
| `GOAL_PROJECAO_ACIMA` | projeção > meta | 1 kWh em 2 h → ritmo de 12 kWh/dia |
| `GOAL_OK` | nenhuma das anteriores | |

- Meta 0 (`daily_goal_mwh = 0`) desliga a meta: sempre `GOAL_OK`.

#### `bool goal_update_power(g, t_us, power_dw)`

- **Faz:** controla o alerta de **potência alta sustentada**.
  - Potência acima de 3000 W: se é a primeira leitura acima, anota quando
    começou (`over_since_us`). Se já está acima há 10 s ou mais → alerta liga.
  - Potência voltou para 3000 W ou menos: zera a contagem (um pico de 9 s não
    dispara nada). O alerta **só desliga abaixo de 2850 W** (95%).
- **Por que 2850 e não 3000?** É a *histerese*: sem ela, uma carga oscilando em
  2990–3010 W ficaria ligando e desligando o alerta sem parar.
- **Devolve:** se o alerta está ativo.

#### `const char *goal_status_name(goal_status_t)`

- Converte o estado em texto para o relatório (`"OK"`, `"ATENCAO"`, ...).

### 7.3 `anomaly.c`: o que está errado na rede, na carga ou no sensor

#### A peça central: `static void debounce(d, condition, persist)`

Quase todo alerta passa por aqui. Um alerta só **liga** depois de `persist` (3)
leituras seguidas com problema e só **desliga** depois de 3 leituras seguidas
normais. Uma leitura ruidosa sozinha não muda nada.

```
leitura:    ok  ruim  ok  ruim ruim ruim  ok  ruim  ok   ok   ok
bad:         0   1    0    1    2    3    0    1    0    0    0
good:        1   0    1    0    0    0    1    0    1    2    3
active:      .   .    .    .    .    ON   ON   ON   ON   ON   off
```

`debounce_reset(d)` zera o contador (usado pelo fator de potência, veja
abaixo).

#### Funções internas de cada alerta

| Função | O que faz |
|---|---|
| `classify_voltage(c, v_dv)` | Diz em qual faixa a tensão está: `VBAND_ADEQUADA` (202–231 V), `VBAND_PRECARIA` (191–202 ou 231–233 V) ou `VBAND_CRITICA` (fora disso). |
| `update_voltage(a, v_dv)` | Passa "abaixo de 202 V" pelo debounce de **subtensão** e "acima de 231 V" pelo de **sobretensão**. Guarda a gravidade (`v_severity`): enquanto o alerta está ativo, mantém a pior faixa da última leitura fora do normal. |
| `update_spike(a, power_dw)` | Detecta **salto de potência** (explicado abaixo). |
| `abs64(x)` | Valor absoluto de um `int64_t`. |

#### Como funciona a detecção de salto (`update_spike`)

1. O código mantém uma **linha de base** (`mean_x16`): uma média móvel
   exponencial da potência, em que cada leitura nova pesa 1/16. Ou seja, a
   linha de base "lembra" mais ou menos das últimas 16 leituras.
2. Mantém também a **variação típica** (`dev_x16`): a média, do mesmo jeito,
   de quanto cada leitura se afasta da linha de base.
3. Durante as **30 primeiras leituras** (`spike_warmup`) só aprende, não
   acusa nada.
4. Depois disso, acusa salto quando `potência − linha de base` é maior que o
   **maior** entre `4 × variação típica` e `1000 W`.
5. Um patamar novo conta **um** evento só: depois de acusar, desarma
   (`spike_armed = false`) e só rearma quando a potência volta para perto da
   linha de base. Como a linha de base também acompanha o salto, um aparelho
   que ligou e ficou ligado deixa de ser "atípico" em alguns segundos.

O `_x16` no nome quer dizer que o valor está multiplicado por 16. É ponto fixo:
os 4 bits de baixo guardam a "parte fracionária" da média, que se perderia nos
inteiros ao dividir por 16 a cada passo.

`spike_now` só fica verdadeiro **na leitura do salto**; `spikes` conta o total
de eventos desde o boot.

#### Funções públicas

| Função | O que faz |
|---|---|
| `anomaly_init(a, cfg)` | Zera tudo, guarda a config e arma a detecção de salto. |
| `anomaly_on_reading(a, m)` | Para cada **leitura válida**: zera o contador de falhas (o alerta de sensor some na hora), atualiza tensão e frequência (59,5–60,5 Hz) pelo debounce, avalia o **fator de potência** (abaixo de 0,92) só se a carga for de 100 W ou mais (com menos que isso, zera o debounce do FP: sem carga, o FP não significa nada) e atualiza o detector de salto. |
| `anomaly_on_read_failure(a)` | Para cada **leitura que falhou**: soma uma falha seguida; na 5ª liga `sensor_offline`. |
| `anomaly_flags(a)` | Devolve uma máscara de bits com os alertas ligados agora (`ANOM_SUBTENSAO`, `ANOM_SOBRETENSAO`, `ANOM_FREQUENCIA`, `ANOM_FP_BAIXO`, `ANOM_SALTO_POTENCIA`, `ANOM_SENSOR_SEM_RESPOSTA`). |
| `anomaly_name(flag)` | Nome do alerta em texto, para o relatório. |
| `voltage_band_name(band)` | `"adequada"`, `"precaria"` ou `"critica"`. |

### 7.4 `processing.c`: junta tudo

Este é o "chefe" de `proc/`: a tarefa `proc` só conversa com ele, e ele
distribui o trabalho para `energy`, `goal` e `anomaly`.

#### Os tipos (`processing.h`)

| Tipo | O que é |
|---|---|
| `proc_config_t` | As três configs juntas (energia, meta, anomalias). |
| `proc_state_t` | Os três estados juntos, mais a última leitura válida (`last`), se já houve alguma (`has_reading`) e os contadores `pzem_ok` / `pzem_fail`. **Só a tarefa `proc` mexe nele.** |
| `proc_snapshot_t` | A **fotografia**: uma cópia "achatada" de tudo que as tarefas de saída precisam (medidas, energia, meta, alertas). É o que vai na fila `s_q_snapshot`. |
| `led_pattern_t` | `LED_ON`, `LED_BLINK_SLOW` ou `LED_BLINK_FAST`. |

#### Funções

| Função | O que faz |
|---|---|
| `proc_default_config(cfg)` | Copia os valores de `energy_config.h` para a `proc_config_t`, convertendo segundos em microssegundos. É o **único** lugar que lê os `CFG_*`. |
| `proc_init(p, cfg, now_us)` | Zera o estado e chama `energy_init`, `goal_init` e `anomaly_init`. |
| `proc_on_pzem(p, t_us, ok, m)` | **A função chamada a cada leitura.** Se `ok`: conta `pzem_ok`, guarda a leitura em `last` e chama `energy_on_sample`, `anomaly_on_reading` e `goal_update_power`. Se falhou: conta `pzem_fail` e chama `energy_tick` e `anomaly_on_read_failure` (a falha **não** mexe na energia nem na última leitura). Nos dois casos, reavalia a meta (`update_goal_energy`). |
| `update_goal_energy(p, t_us)` (`static`) | Atalho que pega a energia e o tempo do dia em `energy` e passa para `goal_update_energy`. |
| `proc_snapshot(p, now_us, out)` | Monta a fotografia a partir do estado. |
| `proc_led_pattern(s)` | Decide o LED a partir da foto, nesta ordem: **pisca rápido** se há qualquer anomalia (fora "sensor sem resposta"), meta estourada ou alerta de potência; **pisca devagar** se o sensor não responde ou ainda não houve nenhuma leitura; senão, **aceso fixo**. |
| `led_pattern_level(pattern, t_us)` | Diz se o LED deve estar aceso **neste instante**. Divide o tempo em fatias: 500 ms para o lento (aceso 500, apagado 500 → 1 Hz) e 100 ms para o rápido (5 Hz). Fatia par = aceso, ímpar = apagado. Como depende só do relógio, a `status_task` não precisa guardar estado do pisca. |

---

## 8. `src/rt/`: o tempo real

### 8.1 `rt_tasks.c`: a tabela das tarefas

É **a fonte única** dos números de tempo real. O firmware cria as tarefas com
estes valores, a análise calcula com eles e um teste confere que o
[diagrama-tarefas.md](diagrama-tarefas.md) mostra os mesmos números.

| Tarefa | Prioridade | Período | Deadline | Orçamento de CPU | Suspensão | Tipo |
|---|---|---|---|---|---|---|
| `proc` | 5 | 500 ms | 50 ms | 4 ms | 0 | hard |
| `pzem` | 3 | 1 s | 500 ms | 3 ms | 220 ms | hard |
| `status` | 2 | 50 ms | 50 ms | 1 ms | 0 | soft |
| `report` | 1 | 2 s | 2 s | 30 ms | 0 | soft |

O que cada coluna quer dizer (campos de `rt_task_param_t`):

- **`priority`**: no FreeRTOS, maior = mais importante.
- **`period_us`**: de quanto em quanto tempo a tarefa é liberada (ou o menor
  intervalo possível entre duas liberações).
- **`deadline_us`**: prazo para terminar, contado da liberação.
- **`budget_us`**: quanto de CPU a tarefa usa, no pior caso. Só entra na
  análise; o firmware não força esse limite.
- **`suspend_us`**: quanto tempo a tarefa pode passar dormindo **no meio** do
  trabalho, esperando E/S (a `pzem` esperando a UART).
- **`hard`**: se entra no critério de aceite ("hard") ou não ("soft").

`RT_ISR_LOAD` é uma "tarefa de mentira" com prioridade 255: representa as
interrupções e o kernel do núcleo 1 (tick, UARTs, gptimer) como 50 µs a cada
1 ms. Só é usada na análise.

O `enum rt_task_id_t` (`RT_TASK_PROC`, `RT_TASK_PZEM`, ...) é o índice usado
em todos os vetores do firmware (`s_mon[]`, `s_task[]`, `RT_TASKS[]`).

### 8.2 `rt_monitor.c`: o cronômetro de cada tarefa

#### O tipo `rt_monitor_t`

| Campo | Guarda |
|---|---|
| `deadline_us` | O prazo da tarefa |
| `jobs` | Quantos trabalhos terminaram |
| `misses` | Quantos estouraram o prazo + quantas liberações foram perdidas |
| `max_response_us` | O pior tempo de resposta já visto (`rmax` no relatório) |

#### Funções

| Função | O que faz |
|---|---|
| `rt_monitor_init(m, deadline_us)` | Zera os contadores e guarda o prazo. |
| `rt_monitor_job_done(m, release_us, done_us)` | Chamada **no fim de cada trabalho**. Calcula `resposta = done − release`, atualiza o máximo, soma 1 em `jobs` e, se `resposta > deadline`, soma 1 em `misses`. Terminar **exatamente** no deadline ainda conta como cumprido. Devolve `true` se cumpriu. |
| `rt_monitor_releases_lost(m, n)` | Soma `n` estouros sem trabalho: liberações que chegaram quando a anterior ainda não tinha terminado (a tarefa "ficou para trás"). |
| `rt_monitor_format(buf, size, name, m)` | Escreve `proc:jobs=601,miss=0,rmax=310us,d=50000us`. Esse formato é lido pelo teste de bancada, então **não pode mudar** sem mexer em `test/spec/lib/bench.mjs`. |

### 8.3 `rt_sched.c`: a prova no papel (só no PC)

Este arquivo **não vai para a placa**: é usado pelo teste `test_rt.c` para
provar que, com os orçamentos da tabela, nenhuma tarefa pode estourar o prazo.

#### `bool rt_sched_rta(tasks, n, response_us)`

Faz a **análise de tempo de resposta** (RTA, *Response Time Analysis*). A
pergunta é: "no pior caso, quanto tempo a tarefa `i` leva do momento em que é
liberada até terminar?". A resposta é o tempo dela mais o tempo que as tarefas
mais prioritárias podem "roubar" nesse intervalo:

```
R = C + S + Σ (para cada tarefa j mais prioritária)  teto((R + J_j) / T_j) × C_j
```

- `C` = CPU da própria tarefa, `S` = tempo que ela dorme esperando E/S.
- `teto(R / T_j)` = quantas vezes a tarefa `j` pode ser liberada dentro de `R`.
- `J_j` (jitter) = atraso extra de `j` quando ela também dorme no meio (como a
  `pzem`): um modelo seguro que supõe o pior caso.

Como `R` aparece dos dois lados, a função **itera**: começa com `R = C + S`,
recalcula, e repete até o valor parar de mudar (convergiu → cabe) ou passar do
deadline (não cabe). Exemplo do teste, com 3 tarefas `(C, T)` = (1, 4), (2, 6)
e (3, 10), para a menos prioritária:

```
R = 3                                       → 3 + teto(3/4)·1  + teto(3/6)·2  = 6
R = 6                                       → 3 + teto(6/4)·1  + teto(6/6)·2  = 7
R = 7                                       → 3 + teto(7/4)·1  + teto(7/6)·2  = 9
R = 9                                       → 3 + teto(9/4)·1  + teto(9/6)·2  = 10
R = 10                                      → 3 + teto(10/4)·1 + teto(10/6)·2 = 10  ✔ parou
```

A função ordena as tarefas da mais para a menos prioritária antes, porque o
jitter de uma tarefa depende do `R` das que estão acima dela. Devolve `true` se
todas couberem e preenche `response_us[i]` com o pior caso de cada uma.

#### `uint32_t rt_sched_utilization_permille(tasks, n)`

- Soma `C / T` de todas as tarefas, em partes por mil: quanto da CPU fica
  ocupado no pior caso. Com a tabela atual, cerca de 9,6%.

#### `static uint64_t ceil_div(a, b)` (interna)

- Divisão arredondando para cima (`teto(a / b)`) em inteiros.

---

## 9. `app/`: os dois programas

### 9.1 `energytrack_main.c`: o firmware do WPS 3.1

#### Variáveis globais (todas `static`, só deste arquivo)

| Variável | O que é | Quem escreve | Quem lê |
|---|---|---|---|
| `s_q_pzem_release` | Fila de 1 lugar com o instante de liberação | ISR do timer | `pzem_task` |
| `s_q_samples` | Fila de 8 lugares com as amostras (`sample_msg_t`) | `pzem_task` | `proc_task` |
| `s_q_snapshot` | Fila de 1 lugar com a fotografia, sempre sobrescrita | `proc_task` | `status_task`, `report_task` |
| `s_task[]` | Os identificadores das 4 tarefas (para medir a pilha) | `init_task` | `report_task` |
| `s_mon[]` | Um `rt_monitor_t` por tarefa | cada tarefa no seu | `report_task` |
| `s_pzem_lost_isr` | Quantas liberações a ISR não conseguiu entregar | ISR | `pzem_task` |
| `s_proc` | O estado do processamento | só `proc_task` | só `proc_task` |

`sample_msg_t` é a mensagem da `pzem` para a `proc`: `ok` (a leitura deu
certo?), `t_us` (quando ficou pronta) e `m` (a medição).

#### Funções, na ordem do fluxo

| Função | O que faz |
|---|---|
| `app_main()` | Ponto de entrada que o ESP-IDF chama. Roda no núcleo 0 e só cria a `init_task` no núcleo 1. |
| `init_task()` | Monta tudo (seção [4.1](#41-o-boot-acontece-uma-vez)). Cria as tarefas a partir de `RT_TASKS[]` (nome e prioridade) e de uma tabela local com a função e o tamanho da pilha de cada uma. Liga o timer por último e se apaga. |
| `create_task_or_die(fn, name, stack, prio)` | Cria uma tarefa presa no núcleo 1 (`xTaskCreatePinnedToCore`). Se faltar memória, chama `fatal()`. |
| `fatal(what)` | Imprime `FATAL: ...` e chama `abort()`: o ESP-IDF mostra o *backtrace* e reinicia. |
| `on_pzem_release(release_us)` | **Interrupção** do timer. Tenta pôr o instante na fila da `pzem`. Se a fila ainda estiver cheia (a leitura anterior não terminou), soma 1 em `s_pzem_lost_isr`. Devolve se acordou tarefa mais prioritária. |
| `pzem_task()` | Laço infinito: espera a liberação na fila → `account_lost()` → `pzem_link_read()` → `send_sample()` → `rt_monitor_job_done()`. |
| `account_lost(mon, isr_count, seen)` | Compara o contador da ISR com o último valor visto e passa a diferença para o monitor. Existe porque a ISR não deve mexer no monitor: cada monitor tem **um só escritor** (a tarefa dona). |
| `send_sample(mon, msg)` | Carimba a hora em `msg->t_us` e põe a amostra na fila da `proc` sem esperar. Se a fila estiver cheia, a amostra se perde e isso conta como estouro da `pzem`. |
| `proc_task()` | Inicializa o processamento (`proc_default_config` + `proc_init`) e entra no laço: espera amostra → `proc_on_pzem()` → `proc_snapshot()` → `xQueueOverwrite()` → `rt_monitor_job_done()`. O "dia 1" começa quando esta tarefa começa. |
| `status_task()` | A cada 50 ms: espia a foto, escolhe o padrão (`proc_led_pattern`; sem foto ainda, pisca devagar), acende ou apaga o LED (`led_pattern_level` + `board_led_set`) e marca o trabalho no monitor. |
| `report_task()` | A cada 2 s: espia a foto, imprime o relatório, a linha `rt t=...`, e o veredito de tempo real (abaixo). |
| `period_ticks(id)` | Converte o período de `RT_TASKS[id]` de µs para *ticks* do FreeRTOS (1 tick = 1 ms). |
| `now_us()` | Hora em µs desde o boot (`esp_timer_get_time`). É o mesmo relógio na ISR e nas tarefas, então dá para subtrair um do outro. |
| `print_measurements(s, uptime_s)` | Linha `[ 602s] PZEM ok=... falha=... \| V=... I=... P=... f=... FP=...`. |
| `print_energy_and_goal(s)` | Linhas de energia (dia, anterior, total, potência média, lacunas recuperadas) e da meta (uso, projeção, estado, limite de potência). |
| `print_anomalies(s)` | Linha `anomalias: ...` com o nome de cada alerta ligado (com a gravidade na tensão) e o total de saltos. |
| `print_rt_line(uptime_s)` | Monta a linha `rt t=...` juntando `rt_monitor_format()` das 4 tarefas, e a linha de memória: heap livre e o mínimo de pilha livre que cada tarefa já teve (`uxTaskGetStackHighWaterMark`). |
| `print_wh(label, mwh)` | Imprime mWh como `Wh` com 3 casas (`45312` → `45.312 Wh`). |
| `hard_tasks_clean()` | `true` se toda tarefa *hard* (`proc` e `pzem`) já rodou pelo menos uma vez e tem `misses == 0`. |

#### O veredito do `report_task`

```
se  hard_tasks_clean()                              (nenhum estouro em proc e pzem)
 e  PZEM com ≥ 95% de leituras válidas
 e  uptime ≥ 600 s
    → imprime ">>> RT PASS <<<" (uma vez) e "RT: OK"
senão se houve estouro → "RT: ESTOURO DE DEADLINE"
senão se o PZEM falha demais → "RT: aguardando PZEM..."
senão → "RT: OK ate agora, faltam N s"
```

Nas tarefas periódicas (`status` e `report`), `xTaskDelayUntil()` devolve
`pdFALSE` quando a hora da próxima liberação **já passou** (a tarefa atrasou
um ciclo inteiro). Isso também é contado como estouro.

Estouro de pilha não tem código nosso: o ESP-IDF detecta
(`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY`) e reinicia mostrando o nome da
tarefa.

### 9.2 `selftest_main.c`: o self-test do WPS 3.2

Uma versão mínima para testar **a ligação** (fios, PZEM, LED, serial) antes do
firmware completo. Não usa `proc/`, `rt/` nem o timer de hardware.

| O que muda em relação ao firmware | Como é no self-test |
|---|---|
| Tarefas | Só `pzem` (prio 3, 1 s), `status` (prio 2, 100 ms) e `report` (prio 1, 2 s) |
| Ritmo da `pzem` | `xTaskDelayUntil` em vez do timer de hardware |
| Comunicação entre tarefas | Sem filas: uma `struct` global `s_stats` com campos `volatile`. Cada campo tem um só escritor (a `pzem`), e ler ou escrever 32 bits alinhados é atômico no processador do ESP32, então não precisa de mutex. |
| LED | Aceso fixo se a última leitura deu certo; pisca a 1 Hz se não |
| Aceite | `>>> SELFTEST PASS <<<` depois de 60 s com no máximo 5% de falhas (`pzem_within_tolerance()`) |

Funções: `app_main()`, `init_task()`, `create_task_or_die()`, `fatal()`,
`pzem_task()`, `status_task()`, `report_task()` e `pzem_within_tolerance()`.
Fazem o mesmo papel das homônimas do firmware, na versão simplificada da
tabela acima.

---

## 10. Quem chama quem

```
app_main
└── init_task
    ├── board_init_console / board_init_led / board_init_pzem_uart      (hal)
    ├── xQueueCreate ×3                                                  (FreeRTOS)
    ├── rt_monitor_init ×4                                               (rt)
    ├── create_task_or_die ×4 ──► proc_task, pzem_task, status_task, report_task
    └── acq_timer_start(1 s, on_pzem_release)                           (hal)

[ISR do gptimer] on_alarm ──► on_pzem_release ──► fila s_q_pzem_release

pzem_task
├── account_lost ──► rt_monitor_releases_lost
├── pzem_link_read                                                       (hal)
│   ├── pzem_build_read_request ──► pzem_crc16
│   ├── uart_flush_input / uart_write_bytes / uart_read_bytes            (ESP-IDF)
│   └── pzem_parse_response ──► pzem_crc16, reg_at, reg32_at
├── send_sample ──► fila s_q_samples
└── rt_monitor_job_done

proc_task
├── proc_default_config, proc_init ──► energy_init, goal_init, anomaly_init
├── proc_on_pzem
│   ├── (ok)    energy_on_sample ──► add_spread ──► proportion, close_day
│   │                            └─► energy_tick ──► close_day
│   │           anomaly_on_reading ──► update_voltage ──► classify_voltage, debounce
│   │                              ├─► debounce (frequência, FP) / debounce_reset
│   │                              └─► update_spike
│   │           goal_update_power
│   ├── (falha) energy_tick, anomaly_on_read_failure
│   └── update_goal_energy ──► goal_update_energy
├── proc_snapshot ──► energy_*_mwh, energy_day_avg_power_dw, anomaly_flags
├── xQueueOverwrite(s_q_snapshot)
└── rt_monitor_job_done

status_task
├── xQueuePeek(s_q_snapshot)
├── proc_led_pattern, led_pattern_level                                  (proc)
├── board_led_set                                                        (hal)
└── rt_monitor_job_done

report_task
├── xQueuePeek(s_q_snapshot)
├── print_measurements / print_energy_and_goal / print_anomalies / print_rt_line
│       └── goal_status_name, anomaly_name, voltage_band_name, rt_monitor_format
├── hard_tasks_clean
└── rt_monitor_job_done
```

---

## 11. Como as tarefas conversam sem brigar

Quando duas tarefas mexem na mesma coisa ao mesmo tempo, o resultado pode sair
corrompido. O projeto evita isso **sem nenhum mutex**, com três regras:

1. **Um dono por recurso.** Só a `pzem` usa a UART2, só a `proc` mexe em
   `s_proc`, só a `status` mexe no LED, só a `report` usa o `printf`. Ninguém
   compartilha, então ninguém briga.
2. **Dados passam por filas, por cópia.** A `pzem` não entrega um ponteiro para
   a leitura: a fila copia a `sample_msg_t` inteira. Quem recebe tem a própria
   cópia.
3. **A fotografia é uma caixa de correio de 1 lugar.** A `proc` usa
   `xQueueOverwrite` (troca o conteúdo, nunca bloqueia) e as tarefas de saída
   usam `xQueuePeek` (leem sem retirar). Elas sempre veem uma foto inteira e
   coerente, nunca metade antiga e metade nova.

Os monitores (`s_mon[]`) são a única exceção a "só o dono lê": a `report` lê os
monitores de todas as tarefas. Isso é seguro porque cada monitor tem **um só
escritor** e os campos são de 32 bits, que o ESP32 lê e escreve de uma vez só.
No pior caso, o relatório mostra um número de um ciclo atrás, nunca um número
quebrado.

E a interrupção do timer **nunca chama** o monitor: ela só conta num
`volatile` (`s_pzem_lost_isr`), e a `pzem_task` repassa a contagem com
`account_lost()`.

| Fila | Tamanho | Envia | Recebe | Se encher |
|---|---|---|---|---|
| `s_q_pzem_release` | 1 | ISR (`xQueueSendFromISR`) | `pzem_task` (`xQueueReceive`) | Liberação perdida → `miss` da `pzem` |
| `s_q_samples` | 8 | `pzem_task` (`xQueueSend`, sem esperar) | `proc_task` (`xQueueReceive`) | Amostra perdida → `miss` da `pzem` |
| `s_q_snapshot` | 1 | `proc_task` (`xQueueOverwrite`) | `status` e `report` (`xQueuePeek`) | Nunca enche: sobrescreve |

---

## 12. Testes e ferramentas

### `test/host/`: testes em C que rodam no PC

Compilados com `gcc` junto com os fontes de `proc/`, `rt/` e
`hal/pzem_modbus.c`. Cada caso imprime `ok N - nome` ou `not ok N - nome`
(formato TAP, definido no mini-harness `tap.h` com a macro `TAP_CHECK`).

| Arquivo | Testa |
|---|---|
| `test_pinmap.c` | `board_pins.h` (só PZEM e LED atribuídos, nenhum pino reservado, cada pino capaz da sua função, nenhuma sobra da Pico W) e `pzem_modbus.c` (CRC e decodificação da resposta) |
| `test_energy.c` | `energy.c`: trapézio, lacunas pelo contador, virada do dia |
| `test_goal.c` | `goal.c`: estados da meta, projeção, limite de potência com histerese |
| `test_anomaly.c` | `anomaly.c`: tensão, frequência, FP, salto, sensor sem resposta |
| `test_processing.c` | `processing.c`: configuração padrão igual à spec, leitura com falha, padrão e frequência do pisca do LED |
| `test_rt.c` | `rt_monitor.c`, `rt_sched.c` e a tabela de `rt_tasks.c` (inclusive a análise do firmware) |

### `test/spec/`: a ponte com a especificação

A especificação fica em `.spec/features/aquisicao-processamento/spec.md`, na
raiz do repositório, com critérios de aceite numerados (`AC-001`, `AC-002`,
...). Cada `*.spec.test.mjs` tem um teste `@spec:AC-xxx` por critério. Ele
compila e roda o teste em C correspondente (`lib/host.mjs`) e exige que os
casos daquele critério tenham passado.

| Arquivo | Critérios |
|---|---|
| `processamento.spec.test.mjs` | AC-001 (falha não estraga o cálculo), AC-015 (LED) |
| `anomalias.spec.test.mjs` | AC-002, AC-011 a AC-014 |
| `energia.spec.test.mjs` | AC-005 a AC-007 |
| `metas.spec.test.mjs` | AC-008 a AC-010 |
| `tempo-real.spec.test.mjs` | AC-016 (monitor), AC-017 (análise) |
| `base-esp32.spec.test.mjs` | AC-019 (mapa de pinos) |
| `firmware.spec.test.mjs` | AC-020 (os dois firmwares compilam sem avisos, pelo PlatformIO) |
| `entregaveis.spec.test.mjs` | AC-021 (diagrama confere com `rt_tasks.c`), AC-018 (log de 10 min na bancada) |

`lib/bench.mjs` lê o log da bancada e confere `rt t=600s` ou mais, `miss=0` em
`proc` e `pzem`, pelo menos 95% de leituras boas e `>>> RT PASS <<<`.

### `test/bench/` e `tools/`

- `test/bench/LEIAME.md` explica a prova de bancada. O log
  `serial-bancada.log` cai nessa pasta.
- `tools/capturar-serial.ps1` acha a porta COM da placa, reinicia o DevKit
  pelo sinal RTS (para o log começar no boot), grava 11 minutos de serial
  nesse arquivo e para sozinho.

---

## 13. Por onde começar a ler

Se é a primeira vez no código, esta ordem evita cair num arquivo sem saber de
onde ele vem:

1. **`app/selftest_main.c`**: o programa mais curto. Mostra tarefas, prioridade,
   `xTaskDelayUntil` e `pzem_link_read()` sem nenhuma conta no meio.
2. **`src/hal/pzem_link.c`** e **`src/hal/pzem_modbus.c`**: como um pedido vira
   uma leitura.
3. **`app/energytrack_main.c`**, de baixo para cima: `app_main` → `init_task` →
   `pzem_task` → `proc_task` → `status_task` → `report_task`.
4. **`src/proc/processing.c`**: a porta de entrada das contas.
5. **`src/proc/energy.c`**, **`goal.c`** e **`anomaly.c`**, com o teste de cada
   um aberto ao lado (os testes em `test/host/` são bons exemplos de uso).
6. **`src/rt/rt_tasks.c`** e **`rt_monitor.c`**, junto com o
   [diagrama-tarefas.md](diagrama-tarefas.md).

### Onde mexer para mudar algo

| Quero... | Mexa em |
|---|---|
| Mudar a meta diária, o limite de potência ou as faixas de tensão | `include/energy_config.h` |
| Usar em rede de 127 V | `CFG_NOMINAL_VOLTAGE` em `include/energy_config.h` |
| Trocar o pino do PZEM ou do LED | `include/board_pins.h` (o teste confere as regras do ESP32) |
| Mudar prioridade, período ou deadline | `src/rt/rt_tasks.c` **e** a tabela de `docs/diagrama-tarefas.md` (um teste confere) |
| Mudar o texto do relatório | Funções `print_*` em `app/energytrack_main.c` (sem mudar o formato da linha `rt t=...`, que o teste de bancada lê) |
| Mudar como o LED pisca | `proc_led_pattern()` e `led_pattern_level()` em `src/proc/processing.c` |
| Mudar CPU, tick ou console | `sdkconfig.defaults` (e apague os `sdkconfig.<ambiente>` gerados) |

Depois de mexer, rode os testes no PC (veja o README). Eles avisam se alguma
conta, limite ou o diagrama deixou de bater com a especificação.
