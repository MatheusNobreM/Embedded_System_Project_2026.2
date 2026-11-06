# Plano de execução — aquisicao-processamento

> gerado por `onp-spec plano` em 2026-11-06 00:40 — NÃO edite à mão;
> mudou tasks.md ou a config? Regenere: `onp-spec plano aquisicao-processamento`

## Resumo — o que vai acontecer

- **5 tarefa(s) pendente(s)**: 5 em 4 faixa(s) paralela(s) + 0 sequencial(is) (8 já concluída(s): T-001, T-002, T-003, T-004, T-005, T-006, T-007, T-008)
- **1 faixa = 1 worktree + 1 branch + 1 janela de contexto limpa** — faixas não compartilham nenhum arquivo entre si
- prefere outra seleção ou uma após a outra? Regenere com `onp-spec plano aquisicao-processamento --paralelizar T-xxx,T-yyy` ou `--sequencial`
- tudo acontece na branch de trabalho `spec/aquisicao-processamento`; levar para a main é decisão sua

### Avisos

- ⚠ T-009 está [em-andamento] — entrou no plano; se já houver trabalho local, commite antes de executar

## Faixas e ondas

### Onda 1 — faixa-1 ∥ faixa-2 ∥ faixa-3

#### faixa-1 — branch `spec/aquisicao-processamento-faixa-1` — worktree `../onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento-faixa-1`

| tarefa | título | modelo | esforço | arquivos |
|---|---|---|---|---|
| T-009 | Diagrama de tarefas, roteiro de bancada e captura do log | `claude-sonnet-5` | medium | `energytrack-fw-3.2/docs/diagrama-tarefas.md`, `energytrack-fw-3.2/docs/como-funciona.md`, `energytrack-fw-3.2/README.md`, `energytrack-fw-3.2/tools/capturar-serial.ps1`, `energytrack-fw-3.2/test/bench/LEIAME.md`, `energytrack-fw-3.2/test/spec/lib/bench.mjs`, `energytrack-fw-3.2/test/spec/entregaveis.spec.test.mjs` |
| T-013 | Tempo real e entregáveis no ESP32 | `claude-sonnet-5` | medium | `energytrack-fw-3.2/src/rt/rt_tasks.c`, `energytrack-fw-3.2/src/rt/rt_tasks.h`, `energytrack-fw-3.2/docs/diagrama-tarefas.md`, `energytrack-fw-3.2/docs/como-funciona.md`, `energytrack-fw-3.2/README.md`, `energytrack-fw-3.2/tools/capturar-serial.ps1`, `energytrack-fw-3.2/test/bench/LEIAME.md` |

#### faixa-2 — branch `spec/aquisicao-processamento-faixa-2` — worktree `../onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento-faixa-2`

| tarefa | título | modelo | esforço | arquivos |
|---|---|---|---|---|
| T-010 | Base ESP32: mapa de pinos, recursos e inicialização da placa | `claude-sonnet-5` | medium | `energytrack-fw-3.2/include/board_pins.h`, `energytrack-fw-3.2/include/board_resources.h`, `energytrack-fw-3.2/src/hal/board_init.c`, `energytrack-fw-3.2/src/hal/board_init.h`, `energytrack-fw-3.2/test/host/test_pinmap.c`, `energytrack-fw-3.2/test/spec/base-esp32.spec.test.mjs`, `.spec/constituicao.md` |

#### faixa-3 — branch `spec/aquisicao-processamento-faixa-3` — worktree `../onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento-faixa-3`

| tarefa | título | modelo | esforço | arquivos |
|---|---|---|---|---|
| T-011 | Aquisição no ESP32: gptimer, PZEM pela UART2 e LM35 por ADC contínuo + DMA | `claude-sonnet-5` | high | `energytrack-fw-3.2/src/hal/acq_timer.c`, `energytrack-fw-3.2/src/hal/acq_timer.h`, `energytrack-fw-3.2/src/hal/adc_dma.c`, `energytrack-fw-3.2/src/hal/adc_dma.h`, `energytrack-fw-3.2/src/hal/adc_frame.c`, `energytrack-fw-3.2/src/hal/adc_frame.h`, `energytrack-fw-3.2/src/hal/pzem_link.c`, `energytrack-fw-3.2/src/hal/pzem_link.h`, `energytrack-fw-3.2/src/proc/temp_sensor.c`, `energytrack-fw-3.2/src/proc/temp_sensor.h`, `energytrack-fw-3.2/test/host/test_temp.c`, `energytrack-fw-3.2/test/spec/aquisicao.spec.test.mjs` |

### Onda 2 — faixa-4

#### faixa-4 — branch `spec/aquisicao-processamento-faixa-4` — worktree `../onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento-faixa-4`

| tarefa | título | modelo | esforço | arquivos |
|---|---|---|---|---|
| T-012 | Firmware e self-test no ESP-IDF, build pelo PlatformIO | `claude-sonnet-5` | high | `energytrack-fw-3.2/app/energytrack_main.c`, `energytrack-fw-3.2/app/selftest_main.c`, `energytrack-fw-3.2/platformio.ini`, `energytrack-fw-3.2/CMakeLists.txt`, `energytrack-fw-3.2/src/CMakeLists.txt`, `energytrack-fw-3.2/sdkconfig.defaults`, `energytrack-fw-3.2/.gitignore`, `energytrack-fw-3.2/.vscode/extensions.json`, `energytrack-fw-3.2/test/spec/firmware.spec.test.mjs`, `onpspec.config.json`, `.vscode/settings.json` |

## Gestão de branches e commits

1. branch de trabalho `spec/aquisicao-processamento` criada do ponto atual (se ainda não existir)
2. cada faixa nasce dela como branch própria e roda no seu worktree — **1 tarefa = 1 commit** (`T-xxx feature: título`)
3. terminou a onda → merge `--no-ff` de cada faixa de volta, na ordem; conflito interrompe a faixa e pede resolução humana
4. faixa mesclada → worktree removido, branch apagada, tarefa marcada `[concluida]` no tasks.md
5. gate final na branch de trabalho: `onp-spec verify aquisicao-processamento` + `onp-spec audit --ci` — **exit 0 ou não está pronto**

## Como executar

### ▶ Execução — Claude Code headless

```bash
bash .spec/features/aquisicao-processamento/executar-tarefas.sh
```

Cada faixa roda `claude -p` com **janela de contexto limpa**, no seu worktree, com
`--model` e `--effort` já definidos por tarefa e permissões `acceptEdits`. Os prompts exatos estão
embutidos no script — quer rodar uma faixa na mão, é só copiá-los de lá.
Logs: `../onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento-logs/`.

### 📣 Acompanhamento — tabela + resumo no chat (a cada 1 min)

O script roda em **background**: o agente AVISA o usuário antes de iniciar e,
enquanto roda, posta no chat a cada ~1 minuto a **tabela de andamento** (qual
tarefa está rodando, qual não está, o que concluiu/falhou) junto com o
**resumo geral de andamento** (escrito por IA; sem IA, o motor resume). Ao
final, o usuário recebe o resumo completo da execução. A qualquer momento:

```bash
onp-spec resumo aquisicao-processamento --tabela   # a tabela de andamento
onp-spec resumo aquisicao-processamento            # o resumo em texto
```

