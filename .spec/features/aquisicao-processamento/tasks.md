# Tasks: Tarefas FreeRTOS de aquisição e processamento (WPS 3.1)

> feature: aquisicao-processamento

<!--
  Status: pendente | em-andamento | concluida
  Ordem lógica: T-001 primeiro (ponte de testes usada por todas);
  T-002..T-005 e T-007 são independentes entre si; T-006 usa T-003..T-005;
  T-008 integra tudo; T-009 fecha os entregáveis.
  Migração para o ESP32 (05/11/2026): T-010 (base) e T-011 (aquisição) vêm
  antes de T-012 (firmware e build), que usa as APIs das duas; T-013 fecha
  tempo real e entregáveis. A versão da Pico W fica na branch pico-w.
-->

## T-001 — Base Pico W pura e ponte de testes com o motor [concluida]
- Refs: US-007, AC-019
- Arquivos: energytrack-fw-3.2/include/board_pins.h, energytrack-fw-3.2/include/board_resources.h, energytrack-fw-3.2/src/hal/board_init.c, energytrack-fw-3.2/src/hal/board_init.h, energytrack-fw-3.2/app/selftest_main.c, energytrack-fw-3.2/test/host/tap.h, energytrack-fw-3.2/test/host/test_pinmap.c, energytrack-fw-3.2/test/spec/lib/host.mjs, energytrack-fw-3.2/test/spec/base-esp32.spec.test.mjs, onpspec.config.json, .spec/constituicao.md
- Notas: remove os módulos da BitDogLab (periph_init, ws2812, oled_probe, adc_shared). Self-test 3.2 com as tarefas pzem, status e report, como no documento. Testes em C saem em TAP; o runner Node compila com gcc e cria um teste por critério de aceite. Na migração (T-010), base-picow.spec.test.mjs virou base-esp32.spec.test.mjs.

## T-002 — Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA [concluida]
- Refs: US-001
- Arquivos: energytrack-fw-3.2/src/hal/acq_timer.c, energytrack-fw-3.2/src/hal/acq_timer.h, energytrack-fw-3.2/src/hal/pzem_link.c, energytrack-fw-3.2/src/hal/pzem_link.h, energytrack-fw-3.2/src/hal/pzem_modbus.c, energytrack-fw-3.2/src/hal/pzem_modbus.h
- Notas: timer de repetição de 1 s (alarme de hardware) libera a tarefa do PZEM; ADC4 em modo contínuo a 1 kHz, dois canais DMA encadeados em ping-pong de 100 amostras, IRQ DMA_IRQ_1 compartilhada. A temperatura e a aquisição por ADC + DMA saíram em T-014 (07/11/2026).

## T-003 — Potência e energia: integração, recuperação de lacuna e virada do dia [concluida]
- Refs: US-003, AC-005, AC-006, AC-007
- Arquivos: energytrack-fw-3.2/src/proc/energy.c, energytrack-fw-3.2/src/proc/energy.h, energytrack-fw-3.2/test/host/test_energy.c, energytrack-fw-3.2/test/spec/energia.spec.test.mjs
- Notas: inteiros com escala fixa (sem float); trapézio entre leituras próximas, contador do PZEM nas lacunas.

## T-004 — Meta diária e limite de potência [concluida]
- Refs: US-004, AC-008, AC-009, AC-010
- Arquivos: energytrack-fw-3.2/src/proc/goal.c, energytrack-fw-3.2/src/proc/goal.h, energytrack-fw-3.2/test/host/test_goal.c, energytrack-fw-3.2/test/spec/metas.spec.test.mjs

## T-005 — Detecção de anomalias [concluida]
- Refs: US-005, AC-002, AC-011, AC-012, AC-013, AC-014
- Arquivos: energytrack-fw-3.2/src/proc/anomaly.c, energytrack-fw-3.2/src/proc/anomaly.h, energytrack-fw-3.2/test/host/test_anomaly.c, energytrack-fw-3.2/test/spec/anomalias.spec.test.mjs
- Notas: O alerta de superaquecimento (AC-004) saiu em T-014 (07/11/2026).

## T-006 — Processamento integrado e LED de estado [concluida]
- Refs: US-001, US-005, AC-001, AC-015
- Arquivos: energytrack-fw-3.2/src/proc/processing.c, energytrack-fw-3.2/src/proc/processing.h, energytrack-fw-3.2/include/energy_config.h, energytrack-fw-3.2/test/host/test_processing.c, energytrack-fw-3.2/test/spec/processamento.spec.test.mjs
- Notas: depende de T-003, T-004 e T-005. energy_config.h concentra metas e limiares.

## T-007 — Monitor de deadline, tabela de tarefas e análise de tempo de resposta [concluida]
- Refs: US-006, AC-016, AC-017
- Arquivos: energytrack-fw-3.2/src/rt/rt_monitor.c, energytrack-fw-3.2/src/rt/rt_monitor.h, energytrack-fw-3.2/src/rt/rt_sched.c, energytrack-fw-3.2/src/rt/rt_sched.h, energytrack-fw-3.2/src/rt/rt_tasks.c, energytrack-fw-3.2/src/rt/rt_tasks.h, energytrack-fw-3.2/test/host/test_rt.c, energytrack-fw-3.2/test/spec/tempo-real.spec.test.mjs

## T-008 — Firmware FreeRTOS: tarefas, filas, ISRs e relatório [concluida]
- Refs: US-006, US-007, AC-020
- Arquivos: energytrack-fw-3.2/app/energytrack_main.c, energytrack-fw-3.2/CMakeLists.txt, energytrack-fw-3.2/test/spec/firmware.spec.test.mjs
- Notas: depende de T-001 a T-007. Prioridades: tarefas do aceite acima das "soft"; entre as do aceite, deadline monotônico. O config/FreeRTOSConfig.h do RP2040 saiu na migração (T-012): no ESP-IDF o FreeRTOS é configurado pelo sdkconfig.

## T-009 — Diagrama de tarefas, roteiro de bancada e captura do log [em-andamento]
- Refs: US-006, US-007, AC-018, AC-021
- Arquivos: energytrack-fw-3.2/docs/diagrama-tarefas.md, energytrack-fw-3.2/docs/como-funciona.md, energytrack-fw-3.2/README.md, energytrack-fw-3.2/tools/capturar-serial.ps1, energytrack-fw-3.2/test/bench/LEIAME.md, energytrack-fw-3.2/test/spec/lib/bench.mjs, energytrack-fw-3.2/test/spec/entregaveis.spec.test.mjs
- Notas: o log da bancada (serial-bancada.log) é capturado por você na placa (agora o ESP32 DevKit); até lá o critério de aceite AC-018 fica sem prova, de propósito.

## T-010 — Base ESP32: mapa de pinos, recursos e inicialização da placa [concluida]
- Refs: US-007, AC-019
- Arquivos: energytrack-fw-3.2/include/board_pins.h, energytrack-fw-3.2/include/board_resources.h, energytrack-fw-3.2/src/hal/board_init.c, energytrack-fw-3.2/src/hal/board_init.h, energytrack-fw-3.2/test/host/test_pinmap.c, energytrack-fw-3.2/test/spec/base-esp32.spec.test.mjs, .spec/constituicao.md
- Notas: PZEM na UART2 (GPIO17 TX / GPIO16 RX), LED no GPIO2, LM35 no GPIO34 (ADC1 canal 6). Reservados: flash SPI (GPIO6–11), console (GPIO1/3), boot (GPIO0/12). Constituição: esperas ocupadas e includes do ESP-IDF entram nos princípios P-004 e P-005. Substitui base-picow.spec.test.mjs.
- Esforço: medio

## T-011 — Aquisição no ESP32: gptimer, PZEM pela UART2 e LM35 por ADC contínuo + DMA [concluida]
- Refs: US-001
- Arquivos: energytrack-fw-3.2/src/hal/acq_timer.c, energytrack-fw-3.2/src/hal/acq_timer.h, energytrack-fw-3.2/src/hal/pzem_link.c, energytrack-fw-3.2/src/hal/pzem_link.h
- Notas: gptimer de 1 MHz com alarme auto-recarregado a cada 1 s. ADC contínuo a 20 kHz (mínimo do ESP32), quadros de 10 ms; adc_frame (C puro) filtra o canal e tira a média de 20 conversões, formando blocos ping-pong de 100 amostras (100 ms). A tarefa adc calibra (eFuse, line fitting) e converte em °C (10 mV/°C). PZEM com o driver de UART do ESP-IDF: a leitura bloqueia no FreeRTOS, sem espera ocupada. O LM35 e o ADC + DMA saíram em T-014 (07/11/2026).
- Esforço: alto

## T-012 — Firmware e self-test no ESP-IDF, build pelo PlatformIO [concluida]
- Refs: US-006, US-007, AC-020
- Arquivos: energytrack-fw-3.2/app/energytrack_main.c, energytrack-fw-3.2/app/selftest_main.c, energytrack-fw-3.2/platformio.ini, energytrack-fw-3.2/CMakeLists.txt, energytrack-fw-3.2/src/CMakeLists.txt, energytrack-fw-3.2/sdkconfig.defaults, energytrack-fw-3.2/.gitignore, energytrack-fw-3.2/.vscode/extensions.json, energytrack-fw-3.2/test/spec/firmware.spec.test.mjs, onpspec.config.json, .vscode/settings.json
- Notas: depende de T-010 e T-011. app_main cria a tarefa init presa no núcleo 1; todas as tarefas e ISRs ficam no núcleo 1. Tick de 1 kHz e console UART0 com driver (printf não faz espera ocupada). Sai o build da Pico: pico_sdk_import.cmake, config/FreeRTOSConfig.h e a configuração .vscode da extensão Raspberry Pi Pico.
- Esforço: alto

## T-013 — Tempo real e entregáveis no ESP32 [concluida]
- Refs: US-006, US-007, AC-017, AC-021
- Arquivos: energytrack-fw-3.2/src/rt/rt_tasks.c, energytrack-fw-3.2/src/rt/rt_tasks.h, energytrack-fw-3.2/docs/diagrama-tarefas.md, energytrack-fw-3.2/docs/como-funciona.md, energytrack-fw-3.2/README.md, energytrack-fw-3.2/tools/capturar-serial.ps1, energytrack-fw-3.2/test/bench/LEIAME.md
- Notas: orçamentos revistos para 240 MHz e console UART a 115200 bps; captura serial pela ponte USB-serial (CP210x/CH340), com reset pelo RTS para o log começar no boot.
- Esforço: medio

## T-014 — Remover o LM35 e a aquisição por ADC + DMA [concluida]
- Refs: US-001, US-005, US-006, US-007, AC-015, AC-017, AC-019, AC-020, AC-021
- Arquivos: energytrack-fw-3.2/include/board_pins.h, energytrack-fw-3.2/include/board_resources.h, energytrack-fw-3.2/include/energy_config.h, energytrack-fw-3.2/src/CMakeLists.txt, energytrack-fw-3.2/src/proc/anomaly.c, energytrack-fw-3.2/src/proc/anomaly.h, energytrack-fw-3.2/src/proc/processing.c, energytrack-fw-3.2/src/proc/processing.h, energytrack-fw-3.2/src/rt/rt_tasks.c, energytrack-fw-3.2/src/rt/rt_tasks.h, energytrack-fw-3.2/src/rt/rt_monitor.h, energytrack-fw-3.2/app/energytrack_main.c, energytrack-fw-3.2/test/host/test_pinmap.c, energytrack-fw-3.2/test/host/test_anomaly.c, energytrack-fw-3.2/test/host/test_processing.c, energytrack-fw-3.2/test/host/test_rt.c, energytrack-fw-3.2/test/spec/base-esp32.spec.test.mjs, energytrack-fw-3.2/test/spec/anomalias.spec.test.mjs, energytrack-fw-3.2/test/spec/processamento.spec.test.mjs, energytrack-fw-3.2/test/spec/entregaveis.spec.test.mjs, energytrack-fw-3.2/test/spec/lib/bench.mjs, energytrack-fw-3.2/docs/diagrama-tarefas.md, energytrack-fw-3.2/docs/como-funciona.md, energytrack-fw-3.2/README.md, energytrack-fw-3.2/test/bench/LEIAME.md, .spec/constituicao.md
- Notas: decisão do usuário (07/11/2026): sem LM35 e sem LED externo; o LED da própria placa (GPIO2) fica. Saem a tarefa adc, os módulos adc_dma, adc_frame e temp_sensor, a história US-002 (AC-003, AC-004) e o alerta SUPERAQUECIMENTO. O proc passa a ser liberado só pelas amostras do PZEM (intervalo mínimo de 1 s).
- Esforço: medio
