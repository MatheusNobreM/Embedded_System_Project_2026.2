# Como o firmware do EnergyTrack funciona

Guia em linguagem simples do código em `energytrack-fw-3.2/`. Serve para quem
sabe C mas nunca mexeu com FreeRTOS nem com o ESP32. Para o diagrama formal
(prioridades, deadlines e a prova de tempo real) veja
[diagrama-tarefas.md](diagrama-tarefas.md); para compilar e testar, o
[README](../README.md); para cada função explicada em detalhe, o
[guia-do-codigo.md](guia-do-codigo.md).

> Até 05/11/2026 este firmware rodava numa Raspberry Pi Pico W. A versão da
> Pico W está na branch `pico-w`; este guia descreve a do ESP32 DevKit.

---

## 1. Em uma frase

O ESP32 lê o medidor de energia PZEM-004T uma vez por segundo e calcula na
hora a potência, a energia do dia, como está a meta de consumo e se tem algo
errado na rede. Depois mostra o resultado no LED da própria placa e manda um
relatório pela porta serial (o cabo USB) a cada 2 segundos. Tudo isso medindo
se cada etapa terminou dentro do prazo (o *deadline*).

## 2. A ideia geral

O firmware funciona como uma pequena linha de produção. Cada "estação" é uma
**tarefa** do FreeRTOS, e as peças passam de uma estação para outra por
**filas**:

```
 QUEM ACORDA                 AQUISIÇÃO              PROCESSAMENTO           SAÍDA

 timer de hardware ──1 s──►  [pzem]  ◄──UART2──► PZEM-004T
                                │
                                └──► fila de amostras ──► [proc] ──► "fotografia" ──┬──► [status] ──► LED (GPIO2)
                                                                                    │
                                                                                    └──► [report] ──► console (printf)
```

- `pzem` **busca os dados** (aquisição).
- `proc` **faz as contas** (processamento).
- `status` e `report` **mostram o resultado** (saída).

Cada recurso tem **um único dono**: só a `pzem` usa a UART2, só a `proc` mexe
no estado das contas, só a `status` mexe no LED e só a `report` escreve no
console. Assim duas tarefas nunca brigam pelo mesmo recurso.

**Dois núcleos, um usado.** O ESP32 tem dois núcleos. Todas as tarefas do
firmware e as interrupções delas ficam presas no **núcleo 1**; o núcleo 0
fica com as tarefas do próprio ESP-IDF (e, num WPS futuro, com o Wi-Fi).
Assim a prova de tempo real (seção 8) continua sendo a de um processador só.

## 3. Seis conceitos que aparecem no código

| Conceito | Em palavras simples |
|---|---|
| **Tarefa** (*task*) | Uma função com um `for (;;)` infinito. O FreeRTOS reveza as tarefas na CPU como se cada uma tivesse o processador só para ela. |
| **Prioridade** | Número da tarefa: maior = mais importante. Se uma tarefa mais prioritária fica pronta, ela **toma a CPU na hora** (preempção). |
| **Fila** (*queue*) | Uma caixa de correio entre tarefas. Quem envia coloca uma mensagem; quem recebe dorme até chegar alguma. Não gasta CPU esperando. |
| **Interrupção** (ISR) | Função chamada pelo hardware no meio de qualquer coisa (o timer estourou, chegou byte na UART). Precisa ser **curtíssima**: aqui ela só anota a hora e põe um aviso numa fila. |
| **Driver** | Código do ESP-IDF que cuida de um periférico (UART, timer). A tarefa pede "me dá 25 bytes" e **dorme** até o driver entregar. |
| **Deadline** | O prazo de cada trabalho. Tempo de resposta = hora que terminou − hora em que foi liberado. Passou do prazo = **estouro** (*miss*). |

## 4. A viagem de uma medição, passo a passo

### 4.1 Leitura do PZEM (a cada 1 segundo)

1. O **timer de hardware** (um *gptimer* do ESP32) estoura e chama
   `on_pzem_release()` (interrupção). Ela coloca a hora atual na fila
   `s_q_pzem_release` e sai.
2. A tarefa **`pzem_task()`** estava dormindo nessa fila. Ela acorda e chama
   `pzem_link_read()`, que:
   - limpa lixo da UART2;
   - envia o pedido Modbus de 8 bytes ("me manda os 10 registradores");
   - **dorme de 10 em 10 ms** dentro do driver da UART (`uart_read_bytes`)
     enquanto os 25 bytes da resposta chegam, por no máximo 200 ms;
   - confere a resposta com `pzem_parse_response()` (CRC, endereço, tamanho) e
     separa tensão, corrente, potência, energia, frequência e fator de potência.
3. A `pzem_task()` monta uma mensagem (`sample_msg_t`) com a leitura, se deu
   certo ou não, e a hora. Envia na **fila de amostras** (`send_sample()`).
4. Ela registra o tempo de resposta (da liberação até aqui) no monitor de
   deadline e volta a dormir.

### 4.2 O processamento (a cada leitura que chega)

A **`proc_task()`** tem a maior prioridade. Ela dorme na fila de amostras e,
para cada mensagem:

- chama `proc_on_pzem()`, que atualiza energia, meta e anomalias (ou só conta a
  falha, se a leitura não veio);
- tira uma **fotografia** do estado (`proc_snapshot()`) e a deixa numa caixa de
  correio de 1 lugar (`s_q_snapshot`). A foto nova sempre substitui a antiga.

### 4.3 A saída

- **`status_task()`**, a cada 50 ms, olha a última foto e decide o LED da
  própria placa (`proc_led_pattern()`): aceso fixo = normal; pisca devagar
  (1 Hz) = PZEM sem resposta; pisca rápido (5 Hz) = anomalia, meta estourada ou
  potência acima do limite.
- **`report_task()`**, a cada 2 s, imprime no console as medidas, a energia, a
  meta, as anomalias e a linha de tempo real (`rt t=...`). Depois de 10 minutos
  sem nenhum estouro ela imprime `>>> RT PASS <<<`. O `printf` cai num buffer
  do driver da UART0, que uma interrupção vai esvaziando: a tarefa não fica
  presa esperando a serial.

## 5. As quatro tarefas

| Tarefa | Prioridade | Quem acorda | Prazo (deadline) | O que faz |
|---|---|---|---|---|
| `proc` | 5 (maior) | chegada de leitura | 50 ms | Contas: energia, meta, anomalias |
| `pzem` | 3 | timer, a cada 1 s | 500 ms | Lê o PZEM pela UART2 |
| `status` | 2 | relógio, a cada 50 ms | — (não crítica) | LED |
| `report` | 1 (menor) | relógio, a cada 2 s | — (não crítica) | Relatório no console |

**Por que nessa ordem?** As duas primeiras fazem parte do critério de aceite
("hard") e por isso ficam acima das duas de saída. Entre elas, quem tem o prazo
mais curto ganha prioridade maior (regra do *deadline monotônico*). A `pzem`
passa a maior parte do tempo dormindo enquanto espera a UART. Por isso, mesmo
com prioridade menor, ela não atrasa ninguém. A prioridade 4 está vaga: era da
tarefa `adc`, que lia um sensor de temperatura e saiu em 07/11/2026.

Existe ainda a tarefa `init` (prioridade 10, núcleo 1). Ela roda uma vez no
boot, monta tudo e se apaga. É ela quem instala os drivers: o ESP-IDF põe a
interrupção de cada driver no núcleo de quem o instalou, então todas ficam no
núcleo 1.

## 6. Arquivo por arquivo

### `app/` — os programas

#### `energytrack_main.c` — o firmware do WPS 3.1

| Função | O que faz |
|---|---|
| `app_main()` | Ponto de entrada do ESP-IDF (o FreeRTOS já está rodando). Cria a tarefa `init` presa no núcleo 1. |
| `init_task()` | Liga o console, o LED e a UART2 e cria as 3 filas, os monitores de deadline e as 4 tarefas. Por último liga o timer do PZEM e se apaga. |
| `on_pzem_release()` | **Interrupção** do timer: põe a hora na fila da `pzem`. Se a fila ainda estiver cheia, conta uma liberação perdida. |
| `pzem_task()` | Lê o PZEM a cada liberação e manda a amostra para a `proc`. |
| `proc_task()` | Processa cada leitura e publica a fotografia. |
| `status_task()` | Acende/apaga o LED conforme o estado. |
| `report_task()` | Imprime o relatório e decide se já dá `RT PASS`. |
| `send_sample()` | Põe a amostra na fila da `proc`. Se a fila estiver cheia, conta estouro de quem enviou. |
| `account_lost()` | Leva para o monitor as liberações que a interrupção não conseguiu entregar. |
| `now_us()` | Hora em µs desde o boot (`esp_timer_get_time`), a mesma nas ISRs e nas tarefas. |
| `period_ticks()` | Converte o período da tabela (µs) em ticks do FreeRTOS. |
| `print_measurements()`, `print_energy_and_goal()`, `print_anomalies()`, `print_rt_line()`, `print_wh()` | Pedaços do relatório. |
| `hard_tasks_clean()` | Verdadeiro se `proc` e `pzem` rodaram e nenhuma estourou o prazo. |
| `create_task_or_die()` | Cria uma tarefa presa no núcleo 1. Se faltar memória, avisa e reinicia (`fatal`). |

Estouro de pilha não precisa de código nosso: o ESP-IDF detecta (opção
`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY`) e reinicia mostrando o nome da tarefa.

#### `selftest_main.c` — o self-test do WPS 3.2

Versão mínima, para testar a ligação: tarefa `pzem` (lê a cada 1 s), `status`
(LED aceso = PZEM responde; piscando = não responde) e `report` (imprime e,
depois de 60 s com no máximo 5% de falhas, diz `SELFTEST PASS`).

### `include/` — configurações do projeto

| Arquivo | Para que serve |
|---|---|
| `board_pins.h` | **Único lugar com números de pino.** PZEM em GPIO17/GPIO16, LED da placa no GPIO2 e os pinos reservados (flash, console, boot). |
| `board_resources.h` | Traduz o mapa para os tipos do ESP-IDF (UART2, GPIO). Também **impede a compilação** se alguém mandar o console para a UART do PZEM. |
| `energy_config.h` | **Todos os limites e metas**: tensão nominal (220 ou 127 V), faixas do PRODIST, meta de 5 kWh/dia, limite de 3000 W por 10 s, frequência e fator de potência. |

### Configuração do ESP-IDF e do build

| Arquivo | Para que serve |
|---|---|
| `platformio.ini` | Dois ambientes, um por firmware (`energytrack_fw` e `energytrack_selftest`), com a plataforma espressif32 fixada (ESP-IDF 6.1). |
| `sdkconfig.defaults` | CPU a 240 MHz, tick do FreeRTOS de 1 ms, console na UART0 a 115200 bps, detecção de estouro de pilha. Substitui o antigo `FreeRTOSConfig.h`. |
| `CMakeLists.txt` e `src/CMakeLists.txt` | Receita do ESP-IDF. O segundo escolhe quais fontes entram em cada firmware. |

### `src/hal/` — conversa com o hardware

| Arquivo | Funções | O que fazem |
|---|---|---|
| `board_init.c` | `board_init_console()` | Instala o driver da UART0 para o `printf` não ficar esperando a serial. |
| | `board_init_pzem_uart()` | Configura a UART2 a 9600 bps nos pinos GPIO17/GPIO16. |
| | `board_init_led()`, `board_led_set(on)` | Configura e acende/apaga o LED do GPIO2. |
| `pzem_modbus.c` | `pzem_build_read_request()` | Monta o pedido Modbus de 8 bytes. |
| | `pzem_parse_response()` | Confere e decodifica a resposta de 25 bytes. |
| | `pzem_crc16()` | Calcula o CRC que detecta bytes corrompidos. |
| `pzem_link.c` | `pzem_link_read()` | A conversa completa pela UART2 (enviar, esperar dormindo, ler, decodificar). |
| `acq_timer.c` | `acq_timer_start()` | Programa o gptimer para chamar uma função a cada 1 s, com a hora exata. |

`pzem_modbus.c` não depende do hardware, então também é testado no PC.

### `src/proc/` — as contas (C puro, testado no PC)

Nenhum arquivo daqui conhece o FreeRTOS ou o ESP-IDF: recebem números e a
hora como parâmetro. Por isso rodam igual na placa e no PC, onde são testados.

| Arquivo | Funções principais | O que fazem |
|---|---|---|
| `energy.c` | `energy_on_sample()` | Soma a energia entre duas leituras. |
| | `energy_tick()` | Vira o dia a cada 24 h, mesmo sem leituras. |
| | `energy_day_mwh()`, `energy_total_mwh()`, `energy_prev_day_mwh()`, `energy_day_avg_power_dw()` | Consultas: energia do dia, desde o boot, do dia anterior e potência média. |
| `goal.c` | `goal_update_energy()` | Situação da meta: OK, PROJECAO_ACIMA, ATENCAO ou ESTOURADA. |
| | `goal_update_power()` | Alerta de potência alta sustentada. |
| `anomaly.c` | `anomaly_on_reading()` | Confere tensão, frequência, fator de potência e salto de potência. |
| | `anomaly_on_read_failure()` | Conta falhas seguidas do sensor. |
| | `anomaly_flags()` | Devolve quais alertas estão ligados. |
| `processing.c` | `proc_on_pzem()` | Distribui cada leitura para energia, meta e anomalias. |
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
| `test/host/*.c` | Testes em C que rodam no PC. Cada caso imprime `ok`/`not ok`. |
| `test/spec/*.mjs` | Ligam cada teste a um critério de aceite da especificação (`@spec:AC-xxx`). |
| `test/bench/` | Onde fica o log da bancada (prova de 10 min na placa). |
| `tools/capturar-serial.ps1` | Reinicia o DevKit e grava o log da serial nesse arquivo. |

## 7. Os cálculos, com números

**Por que números inteiros?** O código nasceu na Pico W, cujo processador
(Cortex-M0+) não tem ponto flutuante. O ESP32 tem, mas manter tudo inteiro
deixa as contas idênticas no PC e na placa e sem arredondamento escondido.
Tudo é inteiro numa unidade pequena, igual ao PZEM:

| Grandeza | Unidade no código | Exemplo |
|---|---|---|
| tensão | dV (0,1 V) | `2205` = 220,5 V |
| potência | dW (0,1 W) | `10000` = 1000 W |
| corrente | mA | `1234` = 1,234 A |
| energia | mWh | `1000000` = 1 kWh |
| frequência | dHz | `600` = 60,0 Hz |
| fator de potência | centésimos | `98` = 0,98 |

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

## 8. Como se prova o "tempo real sem estouro de deadline"

O card pede isso como critério de aceite. Há duas provas que se completam.

**Na placa (medição).** Cada tarefa tem um `rt_monitor_t`. A cada trabalho ela
anota *tempo de resposta = hora do fim − hora da liberação*. A hora da
liberação vem da interrupção do timer ou da chegada da leitura. Se passar do
deadline, ou se uma liberação nova chegar antes da anterior terminar, conta
`miss`. O relatório mostra, por tarefa:

```
rt t=602s proc:jobs=601,miss=0,rmax=310us,d=50000us pzem:... status:... report:...
```

`jobs` = trabalhos feitos, `miss` = estouros, `rmax` = pior tempo de resposta
visto, `d` = deadline. Com 10 min e `miss=0` nas duas tarefas críticas sai
`>>> RT PASS <<<`. O teste da bancada lê esse log.

**No PC (análise).** A **análise de tempo de resposta** (`rt_sched.c`)
calcula, no pior caso, quanto cada tarefa pode demorar: o tempo dela mais o
tempo que as tarefas mais prioritárias (e as interrupções) podem "roubar". Vale
para o núcleo 1, onde está tudo. Com os orçamentos de `rt_tasks.c`, o pior caso
fica bem abaixo dos prazos:

| Tarefa | Pior caso calculado | Prazo |
|---|---|---|
| `proc` | 4,25 ms | 50 ms |
| `pzem` | 239,0 ms | 500 ms |

O núcleo 1 fica ocupado no máximo ~10% do tempo. A medição na placa confirma
que os orçamentos são verdadeiros; a análise mostra que, com eles, não há como
estourar.

## 9. Onde mexer para mudar algo

| Quero... | Mexa em |
|---|---|
| mudar a meta diária ou o limite de potência | `include/energy_config.h` |
| usar em rede de 127 V | `CFG_NOMINAL_VOLTAGE` em `include/energy_config.h` |
| trocar o pino do PZEM ou do LED | `include/board_pins.h` (o teste confere as regras do ESP32) |
| mudar prioridade, período ou deadline | `src/rt/rt_tasks.c` **e** a tabela de `docs/diagrama-tarefas.md` (um teste confere) |
| mudar o que aparece no relatório | funções `print_*` em `app/energytrack_main.c` |
| mudar frequência da CPU, tick ou console | `sdkconfig.defaults` (e apague os `sdkconfig.<ambiente>` gerados) |

Depois de mexer, rode os testes (veja o README). Eles avisam se alguma conta,
limite ou o diagrama deixou de bater com a especificação.
