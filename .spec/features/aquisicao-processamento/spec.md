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

O EnergyTrack mede o consumo elétrico de uma instalação com um ESP32 DevKit
(módulo ESP32-WROOM-32) ligado a um sensor PZEM-004T v3 pela UART2
(GPIO17 TX / GPIO16 RX). Este pacote (WPS/EAP 3.1, card "Estruturar tarefas
FreeRTOS de aquisição e processamento") organiza o firmware em tarefas
FreeRTOS que adquirem dados (UART disparada por timer de hardware) e
processam na própria placa potência, energia, metas e anomalias — com prova
de que o processamento roda em tempo real sem estourar deadline.

Aquisição analógica: em 07/11/2026 o usuário decidiu não usar o LM35 nem LED
externo. A temperatura e a cadeia ADC + DMA (antes US-002, AC-003 e AC-004)
saíram do firmware; o card cita "ADC e DMA", e essa mudança de escopo fica
para combinar com o professor (ASM-013).

Plataforma: em 05/11/2026 o firmware migrou da Raspberry Pi Pico W (RP2040,
pico-sdk) para o ESP32 com ESP-IDF compilado pelo PlatformIO. A versão da
Pico W fica preservada na branch `pico-w`; a do ESP32 vive na branch `esp-32`.

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

- **Dado** o LED da placa (GPIO2 do ESP32 DevKit)
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

- **Dado** o firmware gravado no ESP32 DevKit, com o PZEM ligado à rede e a uma carga pelo transformador de corrente
- **Quando** ele roda 10 min com o log serial capturado em energytrack-fw-3.2/test/bench/serial-bancada.log
- **Então** o último relatório mostra zero estouros nas tarefas proc e pzem, com tempo de resposta máximo abaixo do deadline
- **E** pelo menos 95% das leituras do PZEM são válidas
- **E** aparece a linha ">>> RT PASS <<<"

### US-007 — Firmware e entregáveis para o ESP32 DevKit

Como equipe do EnergyTrack, quero o firmware no ESP32 DevKit (WROOM-32),
compilado com ESP-IDF pelo PlatformIO, e um diagrama de tarefas fiel ao
código, para entregar o WPS 3.1.

#### AC-019 — Mapa de pinos só com o que existe no ESP32 DevKit + PZEM

- **Dado** o mapa de pinos (board_pins.h)
- **Quando** o teste no PC roda
- **Então** só a UART2 do PZEM (GPIO17 TX / GPIO16 RX) e o LED da própria placa (GPIO2) estão atribuídos, cada um num pino capaz da função, e os pinos da flash SPI (GPIO6 a GPIO11), do console (GPIO1/GPIO3) e de boot (GPIO0/GPIO12) estão reservados
- **E** nenhum pino da BitDogLab, da Pico W nem do LM35 sobrou no mapa

#### AC-020 — Firmware compila sem avisos

- **Dado** o PlatformIO com a plataforma espressif32 (ESP-IDF 6.x) instalada
- **Quando** os ambientes energytrack_fw e energytrack_selftest são compilados (pio run)
- **Então** a compilação termina sem erro e sem avisos nos fontes do projeto
- **E** os dois arquivos firmware.bin são gerados

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
- Temperatura e aquisição analógica (LM35 por ADC + DMA, alerta de superaquecimento): removidas em 07/11/2026 por decisão do usuário (eram US-002, AC-003 e AC-004).
- LED externo: o estado aparece só no LED da própria placa (GPIO2).
- Uso do núcleo 0 do ESP32 pelo firmware: fica para o sistema e para o Wi-Fi de outro WPS.

## Suposições

| ID | Suposição | Status | Resolução |
|---|---|---|---|
| ASM-001 | O período da meta é de 24 h contadas a partir do boot (sem RTC/NTP nesta etapa) | confirmada | Usuário escolheu "Diária + limite de potência" sabendo que o período começa no boot (02/11/2026) |
| ASM-002 | Rede de 220 V com as faixas do PRODIST Módulo 8 (adequada 202–231 V; precária 191–202 / 231–233 V; crítica fora disso) | confirmada | Usuário escolheu 220 V (02/11/2026); faixas do PRODIST para 380/220 V |
| ASM-003 | O ADC1 adquire um LM35 (GPIO34, canal 6) em modo contínuo com DMA; como o modo contínuo do ESP32 não amostra abaixo de 20 kHz, cada amostra de 1 kHz é a média de 20 conversões, entregue em blocos ping-pong de 100 amostras | invalidada | Usuário escolheu "Temperatura interna" na Pico W (02/11/2026) e "LM35 externo com ADC contínuo + DMA" no ESP32 (05/11/2026), que não tem sensor interno utilizável pelo ADC com DMA. Invalidada em 07/11/2026: o usuário não vai usar o LM35 (ver ASM-013) |
| ASM-004 | A prova de bancada é de 10 min sem nenhum estouro de deadline | confirmada | Usuário escolheu 10 minutos (02/11/2026) |
| ASM-005 | Meta padrão de 5 kWh/dia e limite de 3000 W sustentado por 10 s, com aviso em 80% | confirmada | Confirmada pelo usuário (02/11/2026) |
| ASM-006 | Deadlines: pzem 500 ms (período 1 s), proc 50 ms após a amostra; status e report são "soft" e não entram no aceite | confirmada | Confirmada pelo usuário (02/11/2026) |
| ASM-007 | Limiares: frequência 59,5–60,5 Hz, fator de potência 0,92 com carga ≥ 100 W, persistência de 3 leituras para os alertas | confirmada | Confirmada pelo usuário (02/11/2026) |
| ASM-008 | O self-test do WPS 3.2 continua como segundo alvo, com as tarefas pzem, status e report | confirmada | Confirmada pelo usuário para a Pico W (02/11/2026); mantido na migração para o ESP32 (05/11/2026) |
| ASM-009 | A placa é um ESP32 DevKit de 30/38 pinos (ESP32-WROOM-32) com LED azul no GPIO2 e console na UART0 pela ponte USB-serial (CP2102/CH340), a 115200 bps | confirmada | Usuário escolheu "ESP32 DevKit (WROOM-32)" (05/11/2026) |
| ASM-010 | O firmware compila com ESP-IDF 6.x (plataforma espressif32 8.0.0) pelo PlatformIO e usa o FreeRTOS do próprio ESP-IDF, com tick de 1 kHz | confirmada | Usuário escolheu "ESP-IDF via PlatformIO" (05/11/2026) |
| ASM-011 | Todas as tarefas do firmware e as interrupções que elas usam (timer e UARTs) ficam presas no núcleo 1 (APP_CPU); o núcleo 0 fica com o sistema. Assim a análise de tempo de resposta de núcleo único continua valendo | confirmada | Usuário confirmou "tudo no núcleo 1" (05/11/2026) |
| ASM-012 | O LM35 é alimentado em 5 V pelo pino VIN/5V do DevKit (com USB) e a saída vai direto no GPIO34, com atenuação de 2,5 dB (até ~1,25 V, ou 125 °C) e a calibração de fábrica do ADC | invalidada | Usuário confirmou o circuito (05/11/2026) e depois decidiu não usar o LM35 (07/11/2026) |
| ASM-013 | A aquisição do WPS 3.1 fica com a UART do PZEM disparada por timer de hardware; o ADC e o DMA saem junto com o LM35, mesmo o card citando "ADC e DMA" | confirmada | Usuário escolheu "Remover ADC + DMA inteiros" (07/11/2026); a aceitação pelo professor fica por conta do usuário |

## Perguntas em aberto

| ID | Pergunta | Status | Resposta |
|---|---|---|---|
| Q-001 | A evidência de bancada (log serial com o relatório de tempo real) basta para o professor, ou ele também quer medição externa (analisador lógico/osciloscópio)? | respondida | O log serial basta (usuário, 02/11/2026) |
