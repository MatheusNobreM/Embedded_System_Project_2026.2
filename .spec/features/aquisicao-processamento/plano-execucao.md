# Plano de execução — aquisicao-processamento

> gerado por `onp-spec plano` em 2026-11-02 18:08 — NÃO edite à mão;
> mudou tasks.md ou a config? Regenere: `onp-spec plano aquisicao-processamento --sequencial`

## Resumo — o que vai acontecer

- **modo SEQUENCIAL (escolha do usuário)**: 9 tarefa(s) pendente(s), UMA APÓS A OUTRA, na árvore principal
- sem worktrees e sem paralelismo — cada tarefa roda numa janela de contexto limpa, na ordem do tasks.md
- tudo acontece na branch de trabalho `spec/aquisicao-processamento`; levar para a main é decisão sua

## Ordem de execução (uma tarefa após a outra)

| tarefa | título | modelo | esforço |
|---|---|---|---|
| T-001 | Base Pico W pura e ponte de testes com o motor | `claude-sonnet-5` | medium |
| T-002 | Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA | `claude-sonnet-5` | medium |
| T-003 | Potência e energia: integração, recuperação de lacuna e virada do dia | `claude-sonnet-5` | medium |
| T-004 | Meta diária e limite de potência | `claude-sonnet-5` | medium |
| T-005 | Detecção de anomalias | `claude-sonnet-5` | medium |
| T-006 | Processamento integrado e LED de estado | `claude-sonnet-5` | medium |
| T-007 | Monitor de deadline, tabela de tarefas e análise de tempo de resposta | `claude-sonnet-5` | medium |
| T-008 | Firmware FreeRTOS: tarefas, filas, ISRs e relatório | `claude-sonnet-5` | medium |
| T-009 | Diagrama de tarefas, roteiro de bancada e captura do log | `claude-sonnet-5` | medium |

## Gestão de branches e commits

1. branch de trabalho `spec/aquisicao-processamento` criada do ponto atual (se ainda não existir)
2. as tarefas rodam nela mesma, na ordem — **1 tarefa = 1 commit** (`T-xxx feature: título`), marcada `[concluida]` só com trabalho feito
3. gate final na branch de trabalho: `onp-spec verify aquisicao-processamento` + `onp-spec audit --ci` — **exit 0 ou não está pronto**

## Como executar

### ▶ Execução — Claude Code headless

```bash
bash .spec/features/aquisicao-processamento/executar-tarefas.sh
```

Cada tarefa roda `claude -p` com **janela de contexto limpa**, na árvore principal,
uma após a outra, com `--model` e `--effort` já definidos por tarefa e permissões `acceptEdits`.
Os prompts exatos estão embutidos no script.
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

