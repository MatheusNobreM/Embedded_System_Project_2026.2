# Spec: Tarefas FreeRTOS de aquisição e processamento (WPS 3.1)

> feature: aquisicao-processamento
> status: em-implementacao

<!--
  Como ler este arquivo (o formato é verificado por `onp-spec audit`):
  - US-xxx = história de usuário · AC-xxx = critério de aceite
    ASM-xxx = suposição · Q-xxx = pergunta em aberto
    São códigos de rastreio: ligam a especificação às tarefas e aos testes.
-->

## Contexto

O EnergyTrack mede o consumo elétrico de uma instalação com uma Raspberry Pi
Pico W (RP2040) ligada a um sensor PZEM-004T v3 pela UART0 (GP0/GP1), sem a
BitDogLab. Este pacote (WPS/EAP 3.1, card "Estruturar tarefas FreeRTOS de
aquisição e processamento") organiza o firmware em tarefas FreeRTOS que
adquirem dados (UART, ADC, timer de hardware e DMA) e processam na própria
placa potência, energia, metas e anomalias — com prova de que o
processamento roda em tempo real sem estourar deadline.

Entregáveis do card: código-fonte do firmware (C) e diagrama de tarefas.
Critério de aceite do card: processamento roda em tempo real sem estouro de
deadline.

## Histórias

### US-001 — Medições elétricas adquiridas no ritmo certo

Como usuário do EnergyTrack, quero que a placa leia do PZEM a tensão, a
corrente, a potência, a energia, a frequência e o fator de potência uma vez
por segundo, disparada por um timer de hardware, para que o consumo seja
acompanhado sem lacunas e sem travar quando o sensor falha.

#### AC-001 — Leitura com falha é contada e não estraga o cálculo

- **Dado** o firmware lendo o PZEM a cada 1 s
- **Quando** uma leitura falha (sem resposta, CRC errado ou resposta incompleta)
- **Então** o relatório conta a falha (PZEM falha=N) e a energia e a última medição válida continuam como estavam
- **E** a leitura válida seguinte é processada normalmente

#### AC-002 — Sensor sem resposta vira alerta depois de 5 falhas seguidas

- **Dado** o firmware lendo o PZEM a cada 1 s
- **Quando** 5 leituras seguidas falham
- **Então** aparece o alerta SENSOR_SEM_RESPOSTA
- **E** o alerta some na primeira leitura válida

### US-002 — Temperatura da placa adquirida por ADC com DMA

Como equipe do firmware, quero amostrar o sensor de temperatura interno do
RP2040 (ADC4) a 1 kHz, ritmado pelo divisor do ADC e copiado por DMA em
ping-pong de 100 amostras, para que a aquisição analógica não gaste CPU e a
placa avise se esquentar.

#### AC-003 — Bloco de amostras do ADC vira temperatura em °C

- **Dado** um bloco de amostras do sensor de temperatura interno
- **Quando** o bloco é convertido
- **Então** a temperatura sai em °C pela fórmula do datasheet do RP2040 (leitura 876 ≈ 27,1 °C; leitura 784 ≈ 70,2 °C; tolerância 0,3 °C)
- **E** a média do bloco usa todas as amostras (sem perder a fração do LSB)

#### AC-004 — Superaquecimento da placa vira alerta

- **Dado** a temperatura da placa sendo medida a cada bloco de 100 ms
- **Quando** ela passa de 70 °C em 3 blocos seguidos
- **Então** aparece o alerta SUPERAQUECIMENTO
- **E** o alerta só some quando a temperatura cai abaixo de 65 °C (histerese)

### US-003 — Potência e energia calculadas na própria placa

Como usuário do EnergyTrack, quero ver a potência instantânea, a potência
média do dia e a energia consumida no dia e desde o boot, calculadas pela
placa, para acompanhar o consumo mesmo sem rede nem aplicativo.

#### AC-005 — Energia é a integral da potência no tempo

- **Dado** leituras válidas chegando a cada 1 s
- **Quando** a carga consome 1000 W constantes durante 1 h
- **Então** a energia do dia mostra 1000 Wh (erro de no máximo 0,1%)
- **E** a potência média do dia mostra 1000 W

#### AC-006 — Intervalo sem leitura é recuperado pelo contador do PZEM

- **Dado** leituras válidas e depois 60 s sem comunicação
- **Quando** a comunicação volta
- **Então** a energia do intervalo sem leitura entra pela diferença do contador interno do PZEM (erro de no máximo 1 Wh)
- **E** o relatório conta 1 intervalo recuperado
- **E** se o contador do PZEM voltou para trás (foi zerado), o intervalo não gera energia negativa

#### AC-007 — O dia vira a cada 24 h

- **Dado** um dia de consumo em andamento
- **Quando** completam 24 h desde o início do dia atual
- **Então** o consumo do dia volta a zero
- **E** o total do dia anterior fica disponível no relatório
- **E** o total desde o boot continua somando

### US-004 — Meta diária de consumo e limite de potência

Como usuário do EnergyTrack, quero uma meta de energia por dia e um limite de
potência, para ser avisado antes e quando eu passar do planejado.

#### AC-008 — Aviso de atenção em 80% e de meta estourada em 100%

- **Dado** uma meta de 5 kWh por dia
- **Quando** o consumo do dia chega a 4 kWh
- **Então** o estado da meta passa a ATENCAO
- **E** ao chegar a 5 kWh passa a ESTOURADA

#### AC-009 — Projeção acima da meta avisa cedo

- **Dado** uma meta de 5 kWh por dia e pelo menos 1 h de dia decorrida
- **Quando** o ritmo médio do dia projeta mais de 5 kWh até o fim do dia
- **Então** o estado da meta passa a PROJECAO_ACIMA mesmo com consumo abaixo de 80%
- **E** com menos de 1 h de dia a projeção não é avaliada

#### AC-010 — Limite de potência sustentada

- **Dado** um limite de 3000 W
- **Quando** a potência fica acima de 3000 W por 10 s seguidos
- **Então** aparece o alerta LIMITE_POTENCIA
- **E** um pico de só 9 s não dispara o alerta
- **E** o alerta some quando a potência cai abaixo de 2850 W (95% do limite)

### US-005 — Anomalias da rede e da carga detectadas na placa

Como usuário do EnergyTrack, quero ser avisado quando a rede ou a carga sair
do normal (tensão, frequência, fator de potência, saltos de consumo), para
agir antes de danos ou desperdício.

#### AC-011 — Tensão fora da faixa adequada do PRODIST (rede de 220 V)

- **Dado** uma rede de 220 V nominais
- **Quando** a tensão fica 3 leituras seguidas abaixo de 202 V ou acima de 231 V
- **Então** aparece o alerta SUBTENSAO ou SOBRETENSAO com a gravidade "precária" (191–202 V / 231–233 V) ou "crítica" (abaixo de 191 V / acima de 233 V)
- **E** o alerta some depois de 3 leituras seguidas na faixa adequada

#### AC-012 — Frequência fora de 59,5–60,5 Hz

- **Dado** uma rede de 60 Hz
- **Quando** a frequência fica 3 leituras seguidas fora de 59,5–60,5 Hz
- **Então** aparece o alerta FREQUENCIA
- **E** o alerta some depois de 3 leituras seguidas dentro da faixa

#### AC-013 — Fator de potência baixo com carga relevante

- **Dado** uma carga de pelo menos 100 W
- **Quando** o fator de potência fica abaixo de 0,92 por 3 leituras seguidas
- **Então** aparece o alerta FP_BAIXO
- **E** com carga abaixo de 100 W o fator de potência não é avaliado

#### AC-014 — Salto de potência atípico

- **Dado** um consumo estável por pelo menos 30 leituras (a linha de base)
- **Quando** a potência salta acima da linha de base mais que o maior entre 4× o desvio típico e 1000 W
- **Então** o evento SALTO_POTENCIA é sinalizado e contado
- **E** variações pequenas em torno da linha de base não disparam o evento

#### AC-015 — LED da placa mostra o estado

- **Dado** o LED da Pico W
- **Quando** tudo está normal
- **Então** o LED fica aceso fixo
- **E** com o sensor sem resposta o LED pisca a 1 Hz
- **E** com qualquer outra anomalia, meta estourada ou limite de potência o LED pisca a 5 Hz

### US-006 — Tarefas FreeRTOS em tempo real sem estouro de deadline

Como equipe do firmware, quero que aquisição e processamento rodem em tarefas
FreeRTOS com período, prioridade e deadline definidos e monitorados, para
provar que o processamento é de tempo real.

#### AC-016 — Monitor acusa estouro de deadline

- **Dado** uma tarefa com deadline D
- **Quando** um job termina depois de D contado da liberação, ou uma liberação é perdida
- **Então** o contador de estouros (miss) da tarefa sobe
- **E** o maior tempo de resposta observado fica registrado

#### AC-017 — Análise de tempo de resposta prova que as tarefas cabem

- **Dado** a tabela de tarefas do firmware (período, deadline, prioridade e orçamento de execução)
- **Quando** a análise de tempo de resposta (RTA, prioridade fixa preemptiva) roda
- **Então** o pior tempo de resposta de cada tarefa fica abaixo do seu deadline
- **E** um conjunto sobrecarregado de propósito é acusado como não escalonável

#### AC-018 — 10 minutos na bancada sem estouro de deadline

- **Dado** o firmware gravado na Pico W, com o PZEM ligado a uma carga
- **Quando** ele roda 10 min com o log serial capturado em energytrack-fw-3.2/test/bench/serial-bancada.log
- **Então** o último relatório mostra zero estouros nas tarefas proc, adc e pzem, com tempo de resposta máximo abaixo do deadline
- **E** pelo menos 95% das leituras do PZEM são válidas
- **E** aparece a linha ">>> RT PASS <<<"

### US-007 — Firmware e entregáveis para a Pico W pura

Como equipe do EnergyTrack, quero o firmware adaptado à Pico W sem BitDogLab e
um diagrama de tarefas fiel ao código, para entregar o WPS 3.1.

#### AC-019 — Mapa de pinos só com o que existe na Pico W + PZEM

- **Dado** o mapa de pinos (board_pins.h)
- **Quando** o teste no PC roda
- **Então** só a UART0 em GP0/GP1 está atribuída e os pinos do chip Wi-Fi (GP23, GP24, GP25, GP29) estão reservados
- **E** nenhum pino da BitDogLab sobrou no mapa

#### AC-020 — Firmware compila sem avisos

- **Dado** o pico-sdk 2.x e o FreeRTOS-Kernel instalados
- **Quando** os alvos energytrack_fw e energytrack_selftest são compilados
- **Então** a compilação termina sem erro e sem avisos nos fontes do projeto
- **E** os dois arquivos .uf2 são gerados

#### AC-021 — Diagrama de tarefas confere com o código

- **Dado** o diagrama em energytrack-fw-3.2/docs/diagrama-tarefas.md
- **Quando** ele é comparado com a tabela de tarefas do firmware
- **Então** cada tarefa do firmware aparece no diagrama com a mesma prioridade, período e deadline

## Fora de escopo

- Wi-Fi, envio de dados e aplicativo (outros WPS).
- Relógio de parede (RTC/NTP): o "dia" conta a partir do boot.
- Persistência em flash: energia e metas zeram no reboot.
- Classificação oficial do PRODIST (média de 10 min por leitura): aqui o alerta é antecipado, com persistência de 3 leituras.
- Ajuste de metas e limiares em tempo de execução: valores fixos em compilação (energy_config.h).
- Calibração do sensor de temperatura interno (tolerância típica do RP2040).

## Suposições

| ID | Suposição | Status | Resolução |
|---|---|---|---|
| ASM-001 | O período da meta é de 24 h contadas a partir do boot (sem RTC/NTP nesta etapa) | confirmada | Usuário escolheu "Diária + limite de potência" sabendo que o período começa no boot (02/11/2026) |
| ASM-002 | Rede de 220 V com as faixas do PRODIST Módulo 8 (adequada 202–231 V; precária 191–202 / 231–233 V; crítica fora disso) | confirmada | Usuário escolheu 220 V (02/11/2026); faixas do PRODIST para 380/220 V |
| ASM-003 | O ADC adquire o sensor de temperatura interno (ADC4) a 1 kHz, por DMA em ping-pong de 100 amostras | confirmada | Usuário escolheu "Temperatura interna" (02/11/2026) |
| ASM-004 | A prova de bancada é de 10 min sem nenhum estouro de deadline | confirmada | Usuário escolheu 10 minutos (02/11/2026) |
| ASM-005 | Meta padrão de 5 kWh/dia e limite de 3000 W sustentado por 10 s, com aviso em 80% | aberta | — |
| ASM-006 | Deadlines: pzem 500 ms (período 1 s), adc 100 ms (um bloco), proc 50 ms após a amostra; status e report são "soft" e não entram no aceite | aberta | — |
| ASM-007 | Limiares: frequência 59,5–60,5 Hz, fator de potência 0,92 com carga ≥ 100 W, superaquecimento 70 °C (some abaixo de 65 °C), persistência de 3 leituras para os alertas | aberta | — |
| ASM-008 | O self-test do WPS 3.2 continua como segundo alvo, adaptado à Pico W como descreve o documento (tarefas pzem, status e report) | aberta | — |

## Perguntas em aberto

| ID | Pergunta | Status | Resposta |
|---|---|---|---|
| Q-001 | A evidência de bancada (log serial com o relatório de tempo real) basta para o professor, ou ele também quer medição externa (analisador lógico/osciloscópio)? | aberta | — |
