# Constituição — v2.0.0

<!--
  Princípios inegociáveis do projeto. Não são estilo: são restrições.
  P-xxx = princípio (código de rastreio, como US/AC/T).
  Níveis: [DEVE] obrigatório · [RECOMENDADO] forte · [PODE] permitido/explícito.
  Todo [DEVE] precisa de verificação executável — senão o audit acusa
  "princípio sem verificação" (PRINCIPIO_SEM_VERIFICACAO). Formatos:
    - verificação(gate): satisfeita pelo próprio audit (só p/ princípios "meta")
    - verificação(teste): @principle:P-xxx
    - verificação(proibido): `regex` em `glob`
    - verificação(obrigatório): `regex` em `glob`
-->

## P-001 [DEVE] Todo requisito tem prova executável

Nenhuma feature é declarada pronta sem o audit em modo CI sair limpo (exit 0).
Este princípio é verificado pelo próprio mecanismo do audit (AC_SEM_TESTE,
AC_SEM_PROVA, TASK_CONCLUIDA_SEM_PROVA) — não precisa de teste extra seu.

- verificação(gate): intrínseca ao audit

## P-002 [RECOMENDADO] Segredos nunca em código

Chaves e senhas vêm de variáveis de ambiente, nunca hard-coded.

- verificação(proibido): `(api[_-]?key|senha|password)\s*[:=]\s*['"][^'"]{8,}` em `energytrack-fw-3.2/**`

## P-003 [DEVE] Número de GPIO só no mapa de pinos

Nenhum arquivo do firmware usa número de GPIO literal: todo pino vem de
`board_pins.h` (BOARD_PIN_*, ou os `gpio_num_t` de `board_resources.h`).
Trocar um fio vira uma edição em um lugar só.

- verificação(proibido): `gpio_[a-z_]+\(\s*\d` em `energytrack-fw-3.2/app/**`
- verificação(proibido): `gpio_[a-z_]+\(\s*\d` em `energytrack-fw-3.2/src/**`

## P-004 [DEVE] Tarefas não fazem espera ocupada

Espera dentro de tarefa é sempre bloqueante do FreeRTOS (vTaskDelay,
xTaskDelayUntil, filas, notificações, ou um driver do ESP-IDF que bloqueia
no FreeRTOS, como uart_read_bytes). As esperas que seguram a CPU — do
ESP-IDF (esp_rom_delay_us, ets_delay_us, usleep) ou as antigas do pico-sdk —
quebrariam a análise de tempo real.

- verificação(proibido): `\b(esp_rom_delay_us|ets_delay_us|usleep|sleep_ms|sleep_us|busy_wait_us|busy_wait_ms)\s*\(` em `energytrack-fw-3.2/app/**`
- verificação(proibido): `\b(esp_rom_delay_us|ets_delay_us|usleep|sleep_ms|sleep_us|busy_wait_us|busy_wait_ms)\s*\(` em `energytrack-fw-3.2/src/**`

## P-005 [DEVE] Processamento e tempo real são C puro, testáveis no PC

O que calcula (energia, metas, anomalias, monitor e análise de tempo real)
não inclui ESP-IDF, pico-sdk nem FreeRTOS: recebe tempo e amostras como
parâmetro e roda igual no host e na placa.

- verificação(proibido): `#\s*include\s*[<"](esp_|driver/|freertos/|soc/|hal/|sdkconfig|pico/|hardware/|FreeRTOS\.h|task\.h|queue\.h|semphr\.h)` em `energytrack-fw-3.2/src/proc/**`
- verificação(proibido): `#\s*include\s*[<"](esp_|driver/|freertos/|soc/|hal/|sdkconfig|pico/|hardware/|FreeRTOS\.h|task\.h|queue\.h|semphr\.h)` em `energytrack-fw-3.2/src/rt/**`
