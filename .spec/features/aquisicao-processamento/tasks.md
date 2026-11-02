# Tasks: Tarefas FreeRTOS de aquisição e processamento (WPS 3.1)

> feature: aquisicao-processamento

<!--
  Status: pendente | em-andamento | concluida
  Ordem lógica: T-001 primeiro (ponte de testes usada por todas);
  T-002..T-005 e T-007 são independentes entre si; T-006 usa T-003..T-005;
  T-008 integra tudo; T-009 fecha os entregáveis.
-->

## T-001 — Base Pico W pura e ponte de testes com o motor [pendente]

- Refs: US-007, AC-019
- Arquivos: energytrack-fw-3.2/include/board_pins.h, energytrack-fw-3.2/include/board_resources.h, energytrack-fw-3.2/src/hal/board_init.c, energytrack-fw-3.2/src/hal/board_init.h, energytrack-fw-3.2/app/selftest_main.c, energytrack-fw-3.2/test/host/tap.h, energytrack-fw-3.2/test/host/test_pinmap.c, energytrack-fw-3.2/test/spec/lib/host.mjs, energytrack-fw-3.2/test/spec/base-picow.spec.test.mjs, onpspec.config.json, .spec/constituicao.md
- Notas: remove os módulos da BitDogLab (periph_init, ws2812, oled_probe, adc_shared). Self-test 3.2 com as tarefas pzem, status e report, como no documento. Testes em C saem em TAP; o runner Node compila com gcc e cria um teste por critério de aceite.

## T-002 — Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA [pendente]

- Refs: US-001, US-002, AC-003
- Arquivos: energytrack-fw-3.2/src/hal/acq_timer.c, energytrack-fw-3.2/src/hal/acq_timer.h, energytrack-fw-3.2/src/hal/adc_dma.c, energytrack-fw-3.2/src/hal/adc_dma.h, energytrack-fw-3.2/src/hal/pzem_link.c, energytrack-fw-3.2/src/hal/pzem_link.h, energytrack-fw-3.2/src/hal/pzem_modbus.c, energytrack-fw-3.2/src/hal/pzem_modbus.h, energytrack-fw-3.2/src/proc/temp_sensor.c, energytrack-fw-3.2/src/proc/temp_sensor.h, energytrack-fw-3.2/test/host/test_temp.c, energytrack-fw-3.2/test/spec/aquisicao.spec.test.mjs
- Notas: timer de repetição de 1 s (alarme de hardware) libera a tarefa do PZEM; ADC4 em modo contínuo a 1 kHz, dois canais DMA encadeados em ping-pong de 100 amostras, IRQ DMA_IRQ_1 compartilhada.

## T-003 — Potência e energia: integração, recuperação de lacuna e virada do dia [pendente]

- Refs: US-003, AC-005, AC-006, AC-007
- Arquivos: energytrack-fw-3.2/src/proc/energy.c, energytrack-fw-3.2/src/proc/energy.h, energytrack-fw-3.2/test/host/test_energy.c, energytrack-fw-3.2/test/spec/energia.spec.test.mjs
- Notas: inteiros com escala fixa (sem float); trapézio entre leituras próximas, contador do PZEM nas lacunas.

## T-004 — Meta diária e limite de potência [pendente]

- Refs: US-004, AC-008, AC-009, AC-010
- Arquivos: energytrack-fw-3.2/src/proc/goal.c, energytrack-fw-3.2/src/proc/goal.h, energytrack-fw-3.2/test/host/test_goal.c, energytrack-fw-3.2/test/spec/metas.spec.test.mjs

## T-005 — Detecção de anomalias [pendente]

- Refs: US-005, AC-002, AC-004, AC-011, AC-012, AC-013, AC-014
- Arquivos: energytrack-fw-3.2/src/proc/anomaly.c, energytrack-fw-3.2/src/proc/anomaly.h, energytrack-fw-3.2/test/host/test_anomaly.c, energytrack-fw-3.2/test/spec/anomalias.spec.test.mjs

## T-006 — Processamento integrado e LED de estado [pendente]

- Refs: US-001, US-005, AC-001, AC-015
- Arquivos: energytrack-fw-3.2/src/proc/processing.c, energytrack-fw-3.2/src/proc/processing.h, energytrack-fw-3.2/include/energy_config.h, energytrack-fw-3.2/test/host/test_processing.c, energytrack-fw-3.2/test/spec/processamento.spec.test.mjs
- Notas: depende de T-003, T-004 e T-005. energy_config.h concentra metas e limiares.

## T-007 — Monitor de deadline, tabela de tarefas e análise de tempo de resposta [pendente]

- Refs: US-006, AC-016, AC-017
- Arquivos: energytrack-fw-3.2/src/rt/rt_monitor.c, energytrack-fw-3.2/src/rt/rt_monitor.h, energytrack-fw-3.2/src/rt/rt_sched.c, energytrack-fw-3.2/src/rt/rt_sched.h, energytrack-fw-3.2/src/rt/rt_tasks.c, energytrack-fw-3.2/src/rt/rt_tasks.h, energytrack-fw-3.2/test/host/test_rt.c, energytrack-fw-3.2/test/spec/tempo-real.spec.test.mjs

## T-008 — Firmware FreeRTOS: tarefas, filas, ISRs e relatório [pendente]

- Refs: US-006, US-007, AC-020
- Arquivos: energytrack-fw-3.2/app/energytrack_main.c, energytrack-fw-3.2/CMakeLists.txt, energytrack-fw-3.2/config/FreeRTOSConfig.h, energytrack-fw-3.2/test/spec/firmware.spec.test.mjs
- Notas: depende de T-001 a T-007. Prioridades: tarefas do aceite acima das "soft"; entre as do aceite, deadline monotônico.

## T-009 — Diagrama de tarefas, roteiro de bancada e captura do log [pendente]

- Refs: US-006, US-007, AC-018, AC-021
- Arquivos: energytrack-fw-3.2/docs/diagrama-tarefas.md, energytrack-fw-3.2/README.md, energytrack-fw-3.2/tools/capturar-serial.ps1, energytrack-fw-3.2/test/bench/LEIAME.md, energytrack-fw-3.2/test/spec/entregaveis.spec.test.mjs
- Notas: o log da bancada (serial-bancada.log) é capturado por você na Pico W; até lá o critério de aceite AC-018 fica sem prova, de propósito.
