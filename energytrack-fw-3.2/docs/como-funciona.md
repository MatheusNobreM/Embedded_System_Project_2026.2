# Como o firmware do EnergyTrack funciona

Guia em linguagem simples do código em `energytrack-fw-3.2/`. Serve para quem
sabe C mas nunca mexeu com FreeRTOS nem com a Pico W. Para o diagrama formal
(prioridades, deadlines e a prova de tempo real) veja
[diagrama-tarefas.md](diagrama-tarefas.md); para compilar e testar, o
[README](../README.md).

---

## 1. Em uma frase

A Pico W lê o medidor de energia PZEM-004T uma vez por segundo, lê a própria
temperatura dez vezes por segundo, calcula na hora a potência, a energia do dia,
como está a meta de consumo e se tem algo errado na rede. Depois mostra o
resultado no LED da placa e manda um relatório pela USB a cada 2 segundos.
Tudo isso medindo se cada etapa terminou dentro do prazo (o *deadline*).

## 2. A ideia geral

O firmware funciona como uma pequena linha de produção. Cada "estação" é uma
**tarefa** do FreeRTOS, e as peças passam de uma estação para outra por
**filas**:

```
 QUEM ACORDA                 AQUISIÇÃO              PROCESSAMENTO           SAÍDA

 timer de hardware ──1 s──►  [pzem]  ◄──UART──► PZEM-004T
                                │
                                ├──► fila de amostras ──► [proc] ──► "fotografia" ──┬──► [status] ──► LED
                                │                                                   │
 DMA do ADC ──────100 ms──►  [adc]   ◄── sensor de temperatura                      └──► [report] ──► USB (printf)
```

- `pzem` e `adc` **buscam os dados** (aquisição).
- `proc` **faz as contas** (processamento).
- `status` e `report` **mostram o resultado** (saída).

Cada recurso tem **um único dono**: só a `pzem` usa a UART, só a `proc` mexe
no estado das contas, só a `status` mexe no LED e só a `report` escreve na
USB. Assim duas tarefas nunca brigam pelo mesmo recurso.

## 3. Seis conceitos que aparecem no código

| Conceito | Em palavras simples |
|---|---|
| **Tarefa** (*task*) | Uma função com um `for (;;)` infinito. O FreeRTOS reveza as tarefas na CPU como se cada uma tivesse o processador só para ela. |
| **Prioridade** | Número da tarefa: maior = mais importante. Se uma tarefa mais prioritária fica pronta, ela **toma a CPU na hora** (preempção). |
| **Fila** (*queue*) | Uma caixa de correio entre tarefas. Quem envia coloca uma mensagem; quem recebe dorme até chegar alguma. Não gasta CPU esperando. |
| **Interrupção** (ISR) | Função chamada pelo hardware no meio de qualquer coisa (o timer estourou, o DMA terminou). Precisa ser **curtíssima**: aqui ela só anota a hora e põe um aviso numa fila. |
| **DMA** | Um "ajudante" do chip que copia dados sozinho (do ADC para a memória), sem usar a CPU. |
| **Deadline** | O prazo de cada trabalho. Tempo de resposta = hora que terminou − hora em que foi liberado. Passou do prazo = **estouro** (*miss*). |

## 4. A viagem de uma medição, passo a passo

### 4.1 Leitura do PZEM (a cada 1 segundo)

1. O **timer de hardware** estoura e chama `on_pzem_release()` (interrupção).
   Ela coloca a hora atual na fila `s_q_pzem_release` e sai.
2. A tarefa **`pzem_task()`** estava dormindo nessa fila. Ela acorda e chama
   `pzem_link_read()`, que:
   - limpa lixo da UART;
   - envia o pedido Modbus de 8 bytes ("me manda os 10 registradores");
   - **dorme de 10 em 10 ms** (`vTaskDelay`) enquanto os 25 bytes da resposta
     chegam, por no máximo 200 ms;
   - confere a resposta com `pzem_parse_response()` (CRC, endereço, tamanho) e
     separa tensão, corrente, potência, energia, frequência e fator de potência.
3. A `pzem_task()` monta uma mensagem (`sample_msg_t`) com a leitura, se deu
   certo ou não, e a hora. Envia na **fila de amostras** (`send_sample()`).
4. Ela registra o tempo de resposta (da liberação até aqui) no monitor de
   deadline e volta a dormir.

### 4.2 Temperatura da placa (a cada 100 ms)

1. O **ADC** converte o sensor de temperatura interno 1000 vezes por segundo,
   no ritmo do próprio relógio do ADC.
2. O **DMA** copia cada conversão para a memória. São dois buffers de 100
   amostras que se alternam (*ping-pong*): enquanto um enche, o outro pode ser lido.
3. Quando um buffer enche, a interrupção do DMA chama `on_adc_block()`, que
   avisa a tarefa `adc` pela fila `s_q_adc_block`.
4. A **`adc_task()`** tira a média das 100 amostras e converte para °C
   (`temp_sensor_block_to_mc()`). Depois manda o valor na fila de amostras.
   Ela tem 100 ms para isso: depois desse tempo, o DMA volta a escrever nesse buffer.

### 4.3 O processamento (a cada amostra que chega)

A **`proc_task()`** tem a maior prioridade. Ela dorme na fila de amostras e,
para cada mensagem:

- se for do PZEM, chama `proc_on_pzem()`, que atualiza energia, meta e anomalias;
- se for temperatura, chama `proc_on_temperature()`, que verifica superaquecimento;
- tira uma **fotografia** do estado (`proc_snapshot()`) e a deixa numa caixa de
  correio de 1 lugar (`s_q_snapshot`). A foto nova sempre substitui a antiga.

### 4.4 A saída

- **`status_task()`**, a cada 50 ms, olha a última foto e decide o LED
  (`proc_led_pattern()`): aceso fixo = normal; pisca devagar (1 Hz) = PZEM sem
  resposta; pisca rápido (5 Hz) = anomalia, meta estourada ou potência acima do limite.
- **`report_task()`**, a cada 2 s, imprime pela USB as medidas, a energia, a
  meta, as anomalias e a linha de tempo real (`rt t=...`). Depois de 10 minutos
  sem nenhum estouro ela imprime `>>> RT PASS <<<`.

## 5. As cinco tarefas

| Tarefa | Prioridade | Quem acorda | Prazo (deadline) | O que faz |
|---|---|---|---|---|
| `proc` | 5 (maior) | chegada de amostra | 50 ms | Contas: energia, meta, anomalias |
| `adc` | 4 | DMA, a cada 100 ms | 100 ms | Média da temperatura |
| `pzem` | 3 | timer, a cada 1 s | 500 ms | Lê o PZEM pela UART |
| `status` | 2 | relógio, a cada 50 ms | — (não crítica) | LED |
| `report` | 1 (menor) | relógio, a cada 2 s | — (não crítica) | Relatório pela USB |

**Por que nessa ordem?** As três primeiras fazem parte do critério de aceite
("hard") e por isso ficam acima das duas de saída. Entre elas, quem tem o prazo
mais curto ganha prioridade maior (regra do *deadline monotônico*). A `pzem`
passa a maior parte do tempo dormindo enquanto espera a UART. Por isso, mesmo
com prioridade menor, ela não atrasa ninguém.

Existe ainda a tarefa `init` (prioridade 7). Ela roda uma vez no boot, monta
tudo e se apaga.

## 6. Arquivo por arquivo

### `app/` — os programas

#### `energytrack_main.c` — o firmware do WPS 3.1

| Função | O que faz |
|---|---|
| `main()` | Liga a USB (`stdio_init_all`), cria a tarefa `init` e inicia o FreeRTOS. |
| `init_task()` | Liga o chip Wi-Fi (para o LED), configura a UART e cria as 4 filas, os monitores de deadline e as 5 tarefas. Por último liga o DMA do ADC e o timer do PZEM, e se apaga. |
| `on_pzem_release()` | **Interrupção** do timer: põe a hora na fila da `pzem`. Se a fila ainda estiver cheia, conta uma liberação perdida. |
| `on_adc_block()` | **Interrupção** do DMA: avisa a `adc` qual buffer encheu e quando. |
| `pzem_task()` | Lê o PZEM a cada liberação e manda a amostra para a `proc`. |
| `adc_task()` | Converte o bloco do ADC em °C e manda para a `proc`. |
| `proc_task()` | Processa cada amostra e publica a fotografia. |
| `status_task()` | Acende/apaga o LED conforme o estado. |
| `report_task()` | Imprime o relatório e decide se já dá `RT PASS`. |
| `send_sample()` | Põe a amostra na fila da `proc`. Se a fila estiver cheia, conta estouro de quem enviou. |
| `account_lost()` | Leva para o monitor as liberações que a interrupção não conseguiu entregar. |
| `rtos_sleep_ms()` | "Dormir" do FreeRTOS (`vTaskDelay`), passado para o `pzem_link_read`. |
| `period_ticks()` | Converte o período da tabela (µs) em ticks do FreeRTOS. |
| `print_measurements()`, `print_energy_and_goal()`, `print_anomalies()`, `print_rt_line()`, `print_wh()` | Pedaços do relatório. |
| `hard_tasks_clean()` | Verdadeiro se `proc`, `adc` e `pzem` rodaram e nenhuma estourou o prazo. |
| `create_task_or_panic()` | Cria uma tarefa. Se faltar memória, trava com mensagem (`panic`). |
| `vApplicationStackOverflowHook()`, `vApplicationMallocFailedHook()` | Chamadas pelo FreeRTOS se uma pilha estourar ou faltar memória: travam com mensagem. |

#### `selftest_main.c` — o self-test do WPS 3.2

Versão mínima, para testar a ligação: tarefa `pzem` (lê a cada 1 s), `status`
(LED aceso = PZEM responde; piscando = não responde) e `report` (imprime e,
depois de 60 s com no máximo 5% de falhas, diz `SELFTEST PASS`).

### `include/` — configurações do projeto

| Arquivo | Para que serve |
|---|---|
| `board_pins.h` | **Único lugar com números de pino.** PZEM em GP0/GP1, canal 4 do ADC (temperatura) e os pinos reservados do chip Wi-Fi. |
| `board_resources.h` | Diz que o PZEM usa a `uart0`. Também **impede a compilação** se alguém ligar o `printf` na UART0, que é do PZEM. |
| `energy_config.h` | **Todos os limites e metas**: tensão nominal (220 ou 127 V), faixas do PRODIST, meta de 5 kWh/dia, limite de 3000 W por 10 s, frequência, fator de potência, temperatura. |

### `config/FreeRTOSConfig.h` — ajustes do FreeRTOS

Tick de 1 ms, até 8 prioridades, 64 KB de memória para tarefas e filas,
detecção de estouro de pilha e integração com o pico-sdk. Cada linha tem um
comentário dizendo por que está ali.

### `src/hal/` — conversa com o hardware

| Arquivo | Funções | O que fazem |
|---|---|---|
| `board_init.c` | `board_init_pzem_uart()` | Configura a UART0 a 9600 bps e os pinos GP0/GP1. |
| | `board_init_cyw43()` | Liga o chip Wi-Fi (o LED da Pico W é um pino dele). |
| | `board_led_set(on)` | Acende ou apaga o LED. |
| `pzem_modbus.c` | `pzem_build_read_request()` | Monta o pedido Modbus de 8 bytes. |
| | `pzem_parse_response()` | Confere e decodifica a resposta de 25 bytes. |
| | `pzem_crc16()` | Calcula o CRC que detecta bytes corrompidos. |
| `pzem_link.c` | `pzem_link_read()` | A conversa completa pela UART (enviar, esperar dormindo, ler, decodificar). |
| `acq_timer.c` | `acq_timer_start()` | Programa o timer de hardware para chamar uma função a cada 1 s, com a hora exata. |
| `adc_dma.c` | `adc_dma_start()` | Liga o ADC no sensor de temperatura a 1 kHz e os dois canais de DMA em ping-pong. |
| | `adc_dma_buffer(i)` | Devolve o buffer 0 ou 1 para leitura. |

`pzem_modbus.c` não depende do hardware, então também é testado no PC.

### `src/proc/` — as contas (C puro, testado no PC)

Nenhum arquivo daqui conhece o FreeRTOS ou o pico-sdk: recebem números e a
hora como parâmetro. Por isso rodam igual na placa e no PC, onde são testados.

| Arquivo | Funções principais | O que fazem |
|---|---|---|
| `temp_sensor.c` | `temp_sensor_block_to_mc()` | Média de um bloco do ADC convertida em m°C pela fórmula do datasheet. |
| `energy.c` | `energy_on_sample()` | Soma a energia entre duas leituras. |
| | `energy_tick()` | Vira o dia a cada 24 h, mesmo sem leituras. |
| | `energy_day_mwh()`, `energy_total_mwh()`, `energy_prev_day_mwh()`, `energy_day_avg_power_dw()` | Consultas: energia do dia, desde o boot, do dia anterior e potência média. |
| `goal.c` | `goal_update_energy()` | Situação da meta: OK, PROJECAO_ACIMA, ATENCAO ou ESTOURADA. |
| | `goal_update_power()` | Alerta de potência alta sustentada. |
| `anomaly.c` | `anomaly_on_reading()` | Confere tensão, frequência, fator de potência e salto de potência. |
| | `anomaly_on_read_failure()` | Conta falhas seguidas do sensor. |
| | `anomaly_on_temperature()` | Confere superaquecimento. |
| | `anomaly_flags()` | Devolve quais alertas estão ligados. |
| `processing.c` | `proc_on_pzem()`, `proc_on_temperature()` | Distribuem cada amostra para energia, meta e anomalias. |
| | `proc_snapshot()` | Monta a fotografia lida pelo LED e pelo relatório. |
| | `proc_led_pattern()`, `led_pattern_level()` | Decidem o pisca do LED. |
| | `proc_default_config()` | Lê os valores de `energy_config.h`. |

### `src/rt/` — tempo real (C puro)

| Arquivo | O que faz |
|---|---|
| `rt_tasks.c` | **A tabela das tarefas**: nome, prioridade, período, deadline e orçamento de CPU. O firmware cria as tarefas a partir dela, e o diagrama é conferido contra ela. |
| `rt_monitor.c` | O "cronômetro" de cada tarefa. `rt_monitor_job_done()` mede o tempo de resposta e conta estouro; `rt_monitor_releases_lost()` conta liberações perdidas; `rt_monitor_format()` escreve o trecho `nome:jobs=...,miss=...` do relatório. |
| `rt_sched.c` | A análise de tempo de resposta (seção 8). Roda só nos testes no PC, não vai para a placa. |

### O resto

| Caminho | Para que serve |
|---|---|
| `CMakeLists.txt` | Receita de compilação dos dois firmwares (`energytrack_fw` e `energytrack_selftest`). |
| `test/host/*.c` | Testes em C que rodam no PC. Cada caso imprime `ok`/`not ok`. |
| `test/spec/*.mjs` | Ligam cada teste a um critério de aceite da especificação (`@spec:AC-xxx`). |
| `test/bench/` | Onde fica o log da bancada (prova de 10 min na placa). |
| `tools/capturar-serial.ps1` | Grava o log da USB da Pico W nesse arquivo. |

## 7. Os cálculos, com números

**Por que números inteiros?** O processador da Pico (Cortex-M0+) não tem
unidade de ponto flutuante: `float` funciona, mas é lento. Então tudo é inteiro
numa unidade pequena, igual ao PZEM:

| Grandeza | Unidade no código | Exemplo |
|---|---|---|
| tensão | dV (0,1 V) | `2205` = 220,5 V |
| potência | dW (0,1 W) | `10000` = 1000 W |
| corrente | mA | `1234` = 1,234 A |
| energia | mWh | `1000000` = 1 kWh |
| frequência | dHz | `600` = 60,0 Hz |
| fator de potência | centésimos | `98` = 0,98 |
| temperatura | m°C | `27100` = 27,1 °C |

**Energia (`energy.c`).** Entre duas leituras, a energia é a área do trapézio:
média das duas potências × tempo. Exemplo: 1000 W e depois 1200 W, um segundo
depois → (1000 + 1200) / 2 × 1 s = 1100 J ≈ 0,306 Wh. O código guarda
`(P1 + P2) × tempo`, ou seja, o dobro, para não perder o "meio" da divisão por 2.

**Leituras perdidas.** Se faltar uma leitura (2 s entre leituras boas), o
trapézio continua valendo. Se faltarem mais de 3 s, o código usa o **contador
de energia do próprio PZEM**, que continua contando mesmo sem a placa ler.
Exemplo: o contador estava em 102 Wh, a comunicação caiu por 60 s e voltou
marcando 119 Wh → somam-se 17 Wh. Se o contador "voltou para trás" (alguém
zerou o PZEM), esse intervalo é ignorado em vez de virar energia negativa.

**Dia.** Ainda não há relógio de parede (RTC/NTP), então o dia é contado a
partir do boot e vira a cada 24 h: o total vai para "dia anterior" e o dia
recomeça do zero.

**Meta (`goal.c`).** Com meta de 5 kWh/dia:
- 4 kWh (80%) → `ATENCAO`;
- 5 kWh (100%) → `ESTOURADA`;
- **projeção**: 1 kWh consumido em 2 h é um ritmo de 12 kWh/dia, acima da meta
  → `PROJECAO_ACIMA`, mesmo com só 20% usado. Só é calculada depois de 1 h de dia.

**Limite de potência.** Acima de 3000 W por **10 segundos seguidos** →
alerta. Um pico de 9 s não conta. O alerta só some abaixo de 2850 W (95%), para
não ficar liga-desliga perto do limite. Isso se chama *histerese*.

**Anomalias (`anomaly.c`).** Quase todas exigem **3 leituras seguidas** para
ligar e 3 para desligar (persistência). Uma leitura ruidosa sozinha não dispara nada.

| Alerta | Quando |
|---|---|
| `SUBTENSAO` / `SOBRETENSAO` | Fora de 202–231 V (faixa adequada do PRODIST para 220 V). Gravidade "precária" até 191 V / 233 V; "crítica" além disso. |
| `FREQUENCIA` | Fora de 59,5–60,5 Hz. |
| `FP_BAIXO` | Fator de potência abaixo de 0,92, só com carga de pelo menos 100 W. |
| `SALTO_POTENCIA` | A potência sobe de repente bem acima da "linha de base" (uma média móvel), mais que 1000 W e mais que 4× a variação normal. Um patamar novo conta **um** evento. |
| `SENSOR_SEM_RESPOSTA` | 5 falhas de leitura seguidas; some na primeira leitura boa. |
| `SUPERAQUECIMENTO` | Placa acima de 70 °C em 3 blocos; só some abaixo de 65 °C. |

**Temperatura (`temp_sensor.c`).** Fórmula do datasheet do RP2040:
T = 27 − (V − 0,706) / 0,001721, com V = leitura × 3,3 / 4096. Uma leitura de
876 dá cerca de 27,1 °C.

## 8. Como se prova o "tempo real sem estouro de deadline"

O card pede isso como critério de aceite. Há duas provas que se completam.

**Na placa (medição).** Cada tarefa tem um `rt_monitor_t`. A cada trabalho ela
anota *tempo de resposta = hora do fim − hora da liberação*. A hora da
liberação vem da interrupção (timer ou DMA) ou da chegada da amostra. Se passar
do deadline, ou se uma liberação nova chegar antes da anterior terminar, conta
`miss`. O relatório mostra, por tarefa:

```
rt t=602s proc:jobs=6612,miss=0,rmax=310us,d=50000us adc:... pzem:...
```

`jobs` = trabalhos feitos, `miss` = estouros, `rmax` = pior tempo de resposta
visto, `d` = deadline. Com 10 min e `miss=0` nas três tarefas críticas sai
`>>> RT PASS <<<`. O teste da bancada lê esse log.

**No PC (análise).** A **análise de tempo de resposta** (`rt_sched.c`)
calcula, no pior caso, quanto cada tarefa pode demorar: o tempo dela mais o
tempo que as tarefas mais prioritárias (e as interrupções) podem "roubar". Com
os orçamentos de `rt_tasks.c`, o pior caso fica bem abaixo dos prazos:

| Tarefa | Pior caso calculado | Prazo |
|---|---|---|
| `proc` | 4,3 ms | 50 ms |
| `adc` | 5,3 ms | 100 ms |
| `pzem` | 250,6 ms | 500 ms |

A CPU fica ocupada no máximo ~14% do tempo. A medição na placa confirma que os
orçamentos são verdadeiros; a análise mostra que, com eles, não há como
estourar.

## 9. Onde mexer para mudar algo

| Quero... | Mexa em |
|---|---|
| mudar a meta diária ou o limite de potência | `include/energy_config.h` |
| usar em rede de 127 V | `CFG_NOMINAL_VOLTAGE` em `include/energy_config.h` |
| trocar o pino do PZEM | `include/board_pins.h` |
| mudar prioridade, período ou deadline | `src/rt/rt_tasks.c` **e** a tabela de `docs/diagrama-tarefas.md` (um teste confere) |
| mudar o que aparece no relatório | funções `print_*` em `app/energytrack_main.c` |

Depois de mexer, rode os testes (veja o README). Eles avisam se alguma conta,
limite ou o diagrama deixou de bater com a especificação.
