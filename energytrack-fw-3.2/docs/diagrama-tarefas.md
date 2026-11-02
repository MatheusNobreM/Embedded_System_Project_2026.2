# Diagrama de tarefas — EnergyTrack WPS 3.1

Firmware `energytrack_fw` na Raspberry Pi Pico W (RP2040, um núcleo, FreeRTOS)
com o PZEM-004T v3 na UART0. A tabela abaixo é conferida automaticamente
contra `src/rt/rt_tasks.c` (teste `entregaveis.spec.test.mjs`): se alguém
mudar prioridade, período ou deadline no código sem atualizar este diagrama,
o teste quebra.

## Fluxo de dados e de liberação

```mermaid
flowchart LR
    subgraph HW["Hardware do RP2040"]
        TMR["Timer de hardware<br/>alarme a cada 1 s"]
        ADC["ADC4 — sensor de temperatura<br/>1 kHz pelo divisor do ADC"]
        DMA["DMA em ping-pong<br/>2 canais × 100 amostras"]
        UART["UART0 · GP0/GP1<br/>9600 bps 8N1"]
        LED["LED da placa<br/>(WL_GPIO0 do CYW43)"]
        USB["USB-CDC<br/>(printf)"]
    end
    PZEMDEV[("PZEM-004T v3")]

    TMR -- "ISR → q_pzem_release (1)" --> PZEM
    ADC --> DMA
    DMA -- "DMA_IRQ_1 → q_adc_block (1)" --> ADCT

    PZEM["pzem · prio 3<br/>T = 1 s · D = 500 ms"] <-- "Modbus-RTU" --> UART
    UART <--> PZEMDEV
    ADCT["adc · prio 4<br/>T = 100 ms · D = 100 ms"]

    PZEM -- "q_samples (8)" --> PROC
    ADCT -- "q_samples (8)" --> PROC
    PROC["proc · prio 5 · D = 50 ms<br/>potência · energia · meta · anomalias"]

    PROC -- "q_snapshot (1, sobrescrita)" --> STATUS
    PROC -- "q_snapshot (1, sobrescrita)" --> REPORT
    STATUS["status · prio 2 · T = 50 ms<br/>(soft)"] --> LED
    REPORT["report · prio 1 · T = 2 s<br/>(soft)"] --> USB
```

- **Um dono por recurso**: só `pzem` usa a UART0, só `adc` lê os buffers do
  DMA, só `proc` mexe no estado do processamento, só `status` mexe no LED e só
  `report` escreve na USB.
- **ISR curta**: as interrupções só carimbam o instante e entregam o evento
  numa fila de 1 posição. Fila cheia = liberação perdida = estouro contado.
- **Caixa de correio**: `q_snapshot` guarda só a fotografia mais recente do
  processamento (`xQueueOverwrite`/`xQueuePeek`), sem mutex.

## Tabela de tarefas

| Tarefa | Prioridade | Período | Deadline | Orçamento de CPU | Suspensão | Tipo | Liberada por |
|---|---|---|---|---|---|---|---|
| `proc` | 5 | 100 ms | 50 ms | 4 ms | 0 ms | hard | fila de amostras (pzem e adc) |
| `adc` | 4 | 100 ms | 100 ms | 1 ms | 0 ms | hard | IRQ do DMA (bloco de 100 amostras) |
| `pzem` | 3 | 1 s | 500 ms | 3 ms | 220 ms | hard | timer de hardware (1 s) |
| `status` | 2 | 50 ms | 50 ms | 1 ms | 0 ms | soft | periódica (`xTaskDelayUntil`) |
| `report` | 1 | 2 s | 2 s | 30 ms | 0 ms | soft | periódica (`xTaskDelayUntil`) |

Interrupções e kernel (tick de 1 kHz, USB, DMA, timer, CYW43) entram na
análise como uma tarefa de prioridade máxima com 50 µs a cada 1 ms (5% da CPU).
Esse orçamento também cobre a tarefa de timers do FreeRTOS (prioridade 7), que
o port do RP2040 usa só para repassar avisos vindos de interrupção.
A tarefa `init` (prioridade 7) só existe no boot: cria filas e tarefas, liga o
DMA e o timer e se apaga.

**Por que estas prioridades**: as tarefas do critério de aceite (hard) ficam
acima das "soft"; entre as hard vale o **deadline monotônico** — deadline menor,
prioridade maior. O período do `proc` é o intervalo mínimo entre amostras da
fonte mais rápida (um bloco do ADC a cada 100 ms); o orçamento de 4 ms cobre um
bloco de ADC e uma leitura do PZEM na mesma janela.

## Prova de que cabe: análise de tempo de resposta

Prioridade fixa preemptiva, com a suspensão do `pzem` (esperando a UART)
tratada como jitter para as tarefas abaixo dele
(`R_i = C_i + S_i + Σ ⌈(R_i + J_j)/T_j⌉·C_j`, com `J_j = R_j − C_j`). Calculado
por `src/rt/rt_sched.c` sobre a tabela acima, no teste do critério AC-017
(roda no PC, não na placa):

| Tarefa | Pior tempo de resposta (R) | Deadline (D) | Folga |
|---|---|---|---|
| `proc` | 4,25 ms | 50 ms | 91% |
| `adc` | 5,3 ms | 100 ms | 95% |
| `pzem` | 250,6 ms | 500 ms | 50% |
| `status` | 9,5 ms | 50 ms | 81% |
| `report` | 41,1 ms | 2 s | 98% |

Utilização total de CPU no pior caso: 13,8%.

## Como o deadline é medido na placa

```mermaid
sequenceDiagram
    participant T as Timer (ISR)
    participant P as pzem (prio 3)
    participant Q as proc (prio 5)
    participant R as report (prio 1)
    T->>P: liberação t0 (q_pzem_release)
    P->>P: Modbus pela UART0 (dorme ~30 ms)
    P->>Q: amostra pronta t1 (q_samples)
    Note over P: resposta do pzem = fim − t0 ≤ 500 ms
    Q->>Q: energia, meta, anomalias
    Note over Q: resposta do proc = fim − t1 ≤ 50 ms
    R->>R: a cada 2 s imprime "rt t=… proc:jobs=…,miss=…,rmax=…us,d=…us …"
```

Cada tarefa tem um `rt_monitor_t` (`src/rt/rt_monitor.c`): tempo de resposta =
término − liberação; acima do deadline, ou liberação perdida, conta `miss`.
Depois de 10 min com `miss=0` nas três tarefas hard e ≥ 95% de leituras válidas
do PZEM, o relatório imprime `>>> RT PASS <<<`.
