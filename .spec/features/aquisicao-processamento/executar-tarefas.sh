#!/usr/bin/env bash
# executar-tarefas.sh — gerado por `onp-spec plano aquisicao-processamento` em 2026-11-02 18:08
# NÃO edite à mão: mudou tasks.md ou a config, regenere o plano.
#
# uso:
#   bash executar-tarefas.sh                  tudo (ondas → sequenciais → gate)
#   bash executar-tarefas.sh --faixa <id>     reexecuta UMA faixa (+ merge + gate)
#   bash executar-tarefas.sh --seq <T-xxx>    reexecuta UMA tarefa sequencial
#   bash executar-tarefas.sh --gate           só o gate (verify + audit)
#   bash executar-tarefas.sh --listar         mostra faixas, tarefas e estados
#   (acrescente --sem-gate para não rodar o gate ao final)
#
# resumo do que está rolando, a qualquer momento: onp-spec resumo aquisicao-processamento
set -u
set -o pipefail

RUN_ID='energytrack-fw-wps3.2-aquisicao-processamento-mvzkomt3'
FEATURE='aquisicao-processamento'
BASE_BRANCH='spec/aquisicao-processamento'
ENGINE='.claude/skills/onp-spec-driven/scripts/onp-spec.mjs'
CLAUDE_FLAGS=(--permission-mode acceptEdits --allowedTools 'Bash(git add:*),Bash(git commit:*),Bash(git status:*),Bash(git diff:*),Bash(git log:*),Bash(node:*)')
STREAM_FLAGS=(--output-format stream-json --verbose)
FALHAS=""
COM_GATE=1
RESUMO_MODEL='claude-haiku-4-5'
RESUMO_PID=""

verde()    { printf '\033[32m%s\033[0m\n' "$*"; }
vermelho() { printf '\033[31m%s\033[0m\n' "$*"; }
amarelo()  { printf '\033[33m%s\033[0m\n' "$*"; }
info()     { printf '· %s\n' "$*"; }
falhar()   { vermelho "✘ $*"; exit 1; }

# eventos vão para o ledger GLOBAL (~/.onp-spec/painel/ledger.jsonl):
# um arquivo para todos os projetos, é o que o onp-spec resumo lê
evento() { node "$ENGINE" evento --run "$RUN_ID" "$@" >/dev/null 2>&1 || true; }

# ── ambiente (todos os modos passam por aqui) ────────────────────────
preparar_ambiente() {
  command -v git >/dev/null 2>&1 || falhar "git não encontrado"
  command -v node >/dev/null 2>&1 || falhar "node não encontrado"
  command -v claude >/dev/null 2>&1 || falhar "Claude Code CLI (claude) não encontrado — instale-o ou siga o modo manual em plano-execucao.md"
  TOPLEVEL=$(git rev-parse --show-toplevel 2>/dev/null) || falhar "fora de um repositório git"
  cd "$TOPLEVEL" || exit 1
  # artefatos recém-gerados pelo `onp-spec plano` são sujeira esperada:
  # se forem a ÚNICA sujeira, o script mesmo commita; qualquer outra, aborta
  if [ -n "$(git status --porcelain)" ]; then
    if [ -z "$(git status --porcelain | grep -v -e 'plano-execucao\.' -e 'plano\.json' -e 'executar-tarefas\.sh')" ]; then
      git add -A
      git commit -q -m "plano de execução: $FEATURE (artefatos gerados)"
      info "artefatos do plano commitados"
    else
      falhar "árvore suja além dos artefatos do plano — commite ou faça git stash antes (os worktrees partem do último commit)"
    fi
  fi
  git ls-files --error-unmatch -- '.spec/features/aquisicao-processamento/spec.md' >/dev/null 2>&1 || falhar "spec.md não está commitada — os worktrees das faixas precisam dela no git"
  ATUAL=$(git rev-parse --abbrev-ref HEAD)
  [ "$ATUAL" != "HEAD" ] || falhar "HEAD destacado — troque para uma branch"
  if [ "$ATUAL" != "$BASE_BRANCH" ]; then
    if git show-ref --verify --quiet "refs/heads/$BASE_BRANCH"; then
      git checkout -q "$BASE_BRANCH" || falhar "não consegui trocar para $BASE_BRANCH"
    else
      git checkout -q -b "$BASE_BRANCH" || falhar "não consegui criar $BASE_BRANCH"
    fi
    info "branch de trabalho: $BASE_BRANCH (a partir de $ATUAL)"
  fi
  git worktree prune
  LOG_DIR="$(dirname "$TOPLEVEL")/onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento-logs"
  WT_BASE="$(dirname "$TOPLEVEL")/onp-worktrees/energytrack-fw-wps3.2-aquisicao-processamento"
  STREAMS_DIR="${ONP_SPEC_HOME:-$HOME/.onp-spec}/painel/streams/$RUN_ID"
  mkdir -p "$LOG_DIR" "$STREAMS_DIR"
}

# worktree limpo mesmo depois de uma tentativa que falhou
preparar_worktree() { # $1=faixa $2=branch $3=worktree
  git worktree prune
  if [ -e "$3" ]; then git worktree remove --force "$3" >/dev/null 2>&1; rm -rf "$3"; fi
  if git show-ref --verify --quiet "refs/heads/$2"; then git branch -D "$2" >/dev/null 2>&1; fi
  git worktree add "$3" -b "$2" >/dev/null 2>&1 || { vermelho "✘ não consegui criar o worktree de $1 em $3"; return 1; }
}

tentativa() { # $1=faixa — conta reexecuções (vai para o ledger)
  local arq="$LOG_DIR/.tentativa-$1"
  local n=1
  [ -f "$arq" ] && n=$(( $(cat "$arq") + 1 ))
  printf "%s" "$n" > "$arq"
  printf "%s" "$n"
}

# uma tarefa = uma sessão claude headless com contexto limpo.
# o JSONL da sessão vira o stream da tarefa no ledger
rodar_tarefa() { # $1=escopo(faixa|seq) $2=T-xxx $3=prompt $4=modelo $5=esforço
  local chave="$1--$2"
  local stream="$STREAMS_DIR/$chave.jsonl"
  evento --tipo tarefa --tarefa "$2" --faixa "$1" --estado executando --stream "$chave"
  info "$2 — claude -p ($4 · $5) · stream: $chave"
  if claude -p "$3" --model "$4" --effort "$5" "${STREAM_FLAGS[@]}" "${CLAUDE_FLAGS[@]}" > "$stream" 2>>"$LOG_DIR/$1.log"; then
    evento --tipo tarefa --tarefa "$2" --faixa "$1" --estado concluida --stream "$chave"
    node "$ENGINE" stream-resumo "$RUN_ID" "$chave" 2>/dev/null || true
    return 0
  fi
  evento --tipo tarefa --tarefa "$2" --faixa "$1" --estado falhou --stream "$chave"
  node "$ENGINE" stream-resumo "$RUN_ID" "$chave" 2>/dev/null || true
  return 1
}

mesclar_faixa() { # $1=faixa $2=branch $3=worktree $4=exit-da-faixa
  if [ "$4" -ne 0 ]; then
    evento --tipo faixa --faixa "$1" --estado falhou
    vermelho "✘ $1 falhou (log: $LOG_DIR/$1.log) — worktree mantido para inspeção: $3"
    amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --faixa $1"
    FALHAS="$FALHAS $1"; return 1
  fi
  evento --tipo faixa --faixa "$1" --estado mesclando
  if git merge --no-ff "$2" -m "merge $1 ($FEATURE)"; then
    git worktree remove --force "$3" >/dev/null 2>&1
    git branch -d "$2" >/dev/null 2>&1
    evento --tipo faixa --faixa "$1" --estado mesclada
    verde "✔ $1 mesclada em $BASE_BRANCH"
  else
    git merge --abort >/dev/null 2>&1
    evento --tipo faixa --faixa "$1" --estado conflito
    vermelho "✘ conflito ao mesclar $1 — resolva na mão: git merge $2 (worktree mantido: $3)"
    FALHAS="$FALHAS $1"; return 1
  fi
}

marcar_concluidas() { # $@=T-xxx
  for t in "$@"; do node "$ENGINE" tarefa "$FEATURE" "$t" concluida >/dev/null || true; done
}

# ── resumo geral de andamento: 1/min enquanto a execução roda ─────────
# escrito por IA (claude -p, sem ferramentas) com fallback do motor; vai
# para o terminal e para o ledger — o agente repassa o texto no chat.
gerar_resumo() {
  local ctx ia
  ctx=$(node "$ENGINE" resumo "$FEATURE" --contexto 2>/dev/null) || ctx=""
  [ -n "$ctx" ] || return 0
  ia=$(claude -p "Você narra, para o dono do produto, uma execução de tarefas de código em andamento. Estado mecânico:

$ctx

Escreva o RESUMO GERAL DE ANDAMENTO: um parágrafo único de 2 a 4 frases, em português simples, dizendo o que está acontecendo agora, o que já terminou, o que falhou e se o usuário precisa agir. Sem markdown, sem listas." --model "$RESUMO_MODEL" 2>/dev/null)
  if [ -n "$ia" ]; then
    node "$ENGINE" resumo "$FEATURE" --gravar --origem ia --texto "$ia" >/dev/null 2>&1 || true
    printf '\n📣 resumo (IA): %s\n' "$ia"
  else
    node "$ENGINE" resumo "$FEATURE" --gravar >/dev/null 2>&1 || true
    printf '\n📣 resumo: %s\n' "$(node "$ENGINE" resumo "$FEATURE" 2>/dev/null)"
  fi
}

# mata o loop E o sleep filho — senão o sleep herda o stdout e quem chamou
# o script via pipe fica esperando EOF por até 60s depois do exit
parar_resumos() {
  [ -n "$RESUMO_PID" ] || return 0
  command -v pkill >/dev/null 2>&1 && pkill -P "$RESUMO_PID" 2>/dev/null
  kill "$RESUMO_PID" 2>/dev/null
  RESUMO_PID=""
}

iniciar_resumos() {
  ( while :; do sleep 60; gerar_resumo; done ) &
  RESUMO_PID=$!
  # ao sair: para o loop e grava um último resumo (o estado final, do motor)
  trap 'parar_resumos; node "$ENGINE" resumo "$FEATURE" --gravar >/dev/null 2>&1 || true' EXIT
}

# ── sequencial T-001 (ordem do tasks.md) ──
executar_seq_T_001() {
  info 'sequencial T-001 — Base Pico W pura e ponte de testes com o motor'
  if rodar_tarefa seq 'T-001' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-001 — "Base Pico W pura e ponte de testes com o motor"
  critérios/refs: AC-019 (Mapa de pinos só com o que existe na Pico W + PZEM)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/include/board_pins.h, energytrack-fw-3.2/include/board_resources.h, energytrack-fw-3.2/src/hal/board_init.c, energytrack-fw-3.2/src/hal/board_init.h, energytrack-fw-3.2/app/selftest_main.c, energytrack-fw-3.2/test/host/tap.h, energytrack-fw-3.2/test/host/test_pinmap.c, energytrack-fw-3.2/test/spec/lib/host.mjs, energytrack-fw-3.2/test/spec/base-picow.spec.test.mjs, onpspec.config.json, .spec/constituicao.md
  mensagem de commit: "T-001 aquisicao-processamento: Base Pico W pura e ponte de testes com o motor"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-001 aquisicao-processamento: Base Pico W pura e ponte de testes com o motor (auto-commit do plano)'
    fi
    marcar_concluidas T-001
    verde "✔ T-001 concluída"
    return 0
  fi
  vermelho "✘ T-001 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-001"
  FALHAS="$FALHAS T-001"
  return 1
}

# ── sequencial T-002 (ordem do tasks.md) ──
executar_seq_T_002() {
  info 'sequencial T-002 — Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA'
  if rodar_tarefa seq 'T-002' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-002 — "Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA"
  critérios/refs: AC-003 (Bloco de amostras do ADC vira temperatura em °C)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/src/hal/acq_timer.c, energytrack-fw-3.2/src/hal/acq_timer.h, energytrack-fw-3.2/src/hal/adc_dma.c, energytrack-fw-3.2/src/hal/adc_dma.h, energytrack-fw-3.2/src/hal/pzem_link.c, energytrack-fw-3.2/src/hal/pzem_link.h, energytrack-fw-3.2/src/hal/pzem_modbus.c, energytrack-fw-3.2/src/hal/pzem_modbus.h, energytrack-fw-3.2/src/proc/temp_sensor.c, energytrack-fw-3.2/src/proc/temp_sensor.h, energytrack-fw-3.2/test/host/test_temp.c, energytrack-fw-3.2/test/spec/aquisicao.spec.test.mjs
  mensagem de commit: "T-002 aquisicao-processamento: Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-002 aquisicao-processamento: Aquisição: timer de hardware, PZEM pela UART e temperatura por ADC + DMA (auto-commit do plano)'
    fi
    marcar_concluidas T-002
    verde "✔ T-002 concluída"
    return 0
  fi
  vermelho "✘ T-002 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-002"
  FALHAS="$FALHAS T-002"
  return 1
}

# ── sequencial T-003 (ordem do tasks.md) ──
executar_seq_T_003() {
  info 'sequencial T-003 — Potência e energia: integração, recuperação de lacuna e virada do dia'
  if rodar_tarefa seq 'T-003' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-003 — "Potência e energia: integração, recuperação de lacuna e virada do dia"
  critérios/refs: AC-005 (Energia é a integral da potência no tempo), AC-006 (Intervalo sem leitura é recuperado pelo contador do PZEM), AC-007 (O dia vira a cada 24 h)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/src/proc/energy.c, energytrack-fw-3.2/src/proc/energy.h, energytrack-fw-3.2/test/host/test_energy.c, energytrack-fw-3.2/test/spec/energia.spec.test.mjs
  mensagem de commit: "T-003 aquisicao-processamento: Potência e energia: integração, recuperação de lacuna e virada do dia"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-003 aquisicao-processamento: Potência e energia: integração, recuperação de lacuna e virada do dia (auto-commit do plano)'
    fi
    marcar_concluidas T-003
    verde "✔ T-003 concluída"
    return 0
  fi
  vermelho "✘ T-003 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-003"
  FALHAS="$FALHAS T-003"
  return 1
}

# ── sequencial T-004 (ordem do tasks.md) ──
executar_seq_T_004() {
  info 'sequencial T-004 — Meta diária e limite de potência'
  if rodar_tarefa seq 'T-004' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-004 — "Meta diária e limite de potência"
  critérios/refs: AC-008 (Aviso de atenção em 80% e de meta estourada em 100%), AC-009 (Projeção acima da meta avisa cedo), AC-010 (Limite de potência sustentada)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/src/proc/goal.c, energytrack-fw-3.2/src/proc/goal.h, energytrack-fw-3.2/test/host/test_goal.c, energytrack-fw-3.2/test/spec/metas.spec.test.mjs
  mensagem de commit: "T-004 aquisicao-processamento: Meta diária e limite de potência"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-004 aquisicao-processamento: Meta diária e limite de potência (auto-commit do plano)'
    fi
    marcar_concluidas T-004
    verde "✔ T-004 concluída"
    return 0
  fi
  vermelho "✘ T-004 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-004"
  FALHAS="$FALHAS T-004"
  return 1
}

# ── sequencial T-005 (ordem do tasks.md) ──
executar_seq_T_005() {
  info 'sequencial T-005 — Detecção de anomalias'
  if rodar_tarefa seq 'T-005' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-005 — "Detecção de anomalias"
  critérios/refs: AC-002 (Sensor sem resposta vira alerta depois de 5 falhas seguidas), AC-004 (Superaquecimento da placa vira alerta), AC-011 (Tensão fora da faixa adequada do PRODIST (rede de 220 V)), AC-012 (Frequência fora de 59,5–60,5 Hz), AC-013 (Fator de potência baixo com carga relevante), AC-014 (Salto de potência atípico)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/src/proc/anomaly.c, energytrack-fw-3.2/src/proc/anomaly.h, energytrack-fw-3.2/test/host/test_anomaly.c, energytrack-fw-3.2/test/spec/anomalias.spec.test.mjs
  mensagem de commit: "T-005 aquisicao-processamento: Detecção de anomalias"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-005 aquisicao-processamento: Detecção de anomalias (auto-commit do plano)'
    fi
    marcar_concluidas T-005
    verde "✔ T-005 concluída"
    return 0
  fi
  vermelho "✘ T-005 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-005"
  FALHAS="$FALHAS T-005"
  return 1
}

# ── sequencial T-006 (ordem do tasks.md) ──
executar_seq_T_006() {
  info 'sequencial T-006 — Processamento integrado e LED de estado'
  if rodar_tarefa seq 'T-006' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-006 — "Processamento integrado e LED de estado"
  critérios/refs: AC-001 (Leitura com falha é contada e não estraga o cálculo), AC-015 (LED da placa mostra o estado)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/src/proc/processing.c, energytrack-fw-3.2/src/proc/processing.h, energytrack-fw-3.2/include/energy_config.h, energytrack-fw-3.2/test/host/test_processing.c, energytrack-fw-3.2/test/spec/processamento.spec.test.mjs
  mensagem de commit: "T-006 aquisicao-processamento: Processamento integrado e LED de estado"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-006 aquisicao-processamento: Processamento integrado e LED de estado (auto-commit do plano)'
    fi
    marcar_concluidas T-006
    verde "✔ T-006 concluída"
    return 0
  fi
  vermelho "✘ T-006 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-006"
  FALHAS="$FALHAS T-006"
  return 1
}

# ── sequencial T-007 (ordem do tasks.md) ──
executar_seq_T_007() {
  info 'sequencial T-007 — Monitor de deadline, tabela de tarefas e análise de tempo de resposta'
  if rodar_tarefa seq 'T-007' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-007 — "Monitor de deadline, tabela de tarefas e análise de tempo de resposta"
  critérios/refs: AC-016 (Monitor acusa estouro de deadline), AC-017 (Análise de tempo de resposta prova que as tarefas cabem)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/src/rt/rt_monitor.c, energytrack-fw-3.2/src/rt/rt_monitor.h, energytrack-fw-3.2/src/rt/rt_sched.c, energytrack-fw-3.2/src/rt/rt_sched.h, energytrack-fw-3.2/src/rt/rt_tasks.c, energytrack-fw-3.2/src/rt/rt_tasks.h, energytrack-fw-3.2/test/host/test_rt.c, energytrack-fw-3.2/test/spec/tempo-real.spec.test.mjs
  mensagem de commit: "T-007 aquisicao-processamento: Monitor de deadline, tabela de tarefas e análise de tempo de resposta"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-007 aquisicao-processamento: Monitor de deadline, tabela de tarefas e análise de tempo de resposta (auto-commit do plano)'
    fi
    marcar_concluidas T-007
    verde "✔ T-007 concluída"
    return 0
  fi
  vermelho "✘ T-007 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-007"
  FALHAS="$FALHAS T-007"
  return 1
}

# ── sequencial T-008 (ordem do tasks.md) ──
executar_seq_T_008() {
  info 'sequencial T-008 — Firmware FreeRTOS: tarefas, filas, ISRs e relatório'
  if rodar_tarefa seq 'T-008' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-008 — "Firmware FreeRTOS: tarefas, filas, ISRs e relatório"
  critérios/refs: AC-020 (Firmware compila sem avisos)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/app/energytrack_main.c, energytrack-fw-3.2/CMakeLists.txt, energytrack-fw-3.2/config/FreeRTOSConfig.h, energytrack-fw-3.2/test/spec/firmware.spec.test.mjs
  mensagem de commit: "T-008 aquisicao-processamento: Firmware FreeRTOS: tarefas, filas, ISRs e relatório"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-008 aquisicao-processamento: Firmware FreeRTOS: tarefas, filas, ISRs e relatório (auto-commit do plano)'
    fi
    marcar_concluidas T-008
    verde "✔ T-008 concluída"
    return 0
  fi
  vermelho "✘ T-008 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-008"
  FALHAS="$FALHAS T-008"
  return 1
}

# ── sequencial T-009 (ordem do tasks.md) ──
executar_seq_T_009() {
  info 'sequencial T-009 — Diagrama de tarefas, roteiro de bancada e captura do log'
  if rodar_tarefa seq 'T-009' 'Você executa UMA tarefa da feature "aquisicao-processamento" (fluxo onp-spec, spec-anchored).
Leia primeiro: .spec/features/aquisicao-processamento/spec.md, .spec/features/aquisicao-processamento/tasks.md e .spec/constituicao.md.

Sua tarefa (somente ela):
T-009 — "Diagrama de tarefas, roteiro de bancada e captura do log"
  critérios/refs: AC-018 (10 minutos na bancada sem estouro de deadline), AC-021 (Diagrama de tarefas confere com o código)
  arquivos permitidos (e seus testes): energytrack-fw-3.2/docs/diagrama-tarefas.md, energytrack-fw-3.2/README.md, energytrack-fw-3.2/tools/capturar-serial.ps1, energytrack-fw-3.2/test/bench/LEIAME.md, energytrack-fw-3.2/test/spec/entregaveis.spec.test.mjs
  mensagem de commit: "T-009 aquisicao-processamento: Diagrama de tarefas, roteiro de bancada e captura do log"

Regras inegociáveis:
- Todo critério de aceite referenciado vira teste com @spec:AC-xxx no título.
- NUNCA enfraqueça, pule (skip/todo) ou apague um teste para passar — teste pulado não é prova e o audit acusa.
- Rode os testes localmente com `node --test --test-reporter=tap "energytrack-fw-3.2/test/spec/*.spec.test.mjs"` até passarem.
- NÃO edite tasks.md, NÃO rode onp-spec verify/audit e NÃO toque em outras tarefas — o orquestrador cuida disso.
- Ao final de CADA tarefa: `git add` só no que você tocou e um commit próprio.' 'claude-sonnet-5' medium >> "$LOG_DIR/seq.log" 2>&1; then
    # commit de segurança se o agente esqueceu (rastreabilidade > perfeição)
    if [ -n "$(git status --porcelain)" ]; then
      git add -A && git commit -q -m 'T-009 aquisicao-processamento: Diagrama de tarefas, roteiro de bancada e captura do log (auto-commit do plano)'
    fi
    marcar_concluidas T-009
    verde "✔ T-009 concluída"
    return 0
  fi
  vermelho "✘ T-009 falhou (log: $LOG_DIR/seq.log)"
  amarelo "  reexecute só ela: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --seq T-009"
  FALHAS="$FALHAS T-009"
  return 1
}

# ── gate: quem decide é a máquina ────────────────────────────────────
rodar_gate() {
  echo
  info "gate: verify + audit --ci"
  evento --tipo gate --etapa inicio
  node "$ENGINE" verify "$FEATURE"
  local v=$?
  evento --tipo gate --etapa verify --exit "$v"
  node "$ENGINE" audit --ci
  AUDIT=$?
  evento --tipo gate --etapa audit --exit "$AUDIT"
  # fecha a contabilidade: status das tarefas + prova do verify no git
  if [ -n "$(git status --porcelain -- '.spec')" ]; then
    git add -A -- '.spec'
    git commit -q -m "$FEATURE: status das tarefas + prova do verify (plano)"
    info "status das tarefas e prova do verify commitados"
  fi
  return "$AUDIT"
}

encerrar() { # $1=escopo
  echo
  if [ -n "$FALHAS" ]; then vermelho "faixas/tarefas com falha:$FALHAS"; fi
  # sem gate não existe veredito: NUNCA anunciar alinhamento sem o audit
  if [ "$COM_GATE" -eq 0 ]; then
    evento --tipo fim --exit 1 --escopo "$1"
    if [ -z "$FALHAS" ]; then
      amarelo "○ trabalho de '$1' terminou SEM o gate (--sem-gate) — isto NÃO é prova de nada"
      amarelo "  para o veredito: bash .spec/features/aquisicao-processamento/executar-tarefas.sh --gate"
      exit 0
    fi
    vermelho "e ainda há falhas — conserte e rode o gate"
    exit 1
  fi
  rodar_gate
  local audit=$?
  if [ "$audit" -eq 0 ] && [ -z "$FALHAS" ]; then
    evento --tipo fim --exit 0 --escopo "$1"
    verde "✔ plano concluído — especificação e código alinhados (audit exit 0) na branch $BASE_BRANCH"
    info "próximo passo: revise e leve para a main quando quiser (git merge $BASE_BRANCH)"
    exit 0
  fi
  evento --tipo fim --exit 1 --escopo "$1"
  vermelho "plano terminou com pendências — leia a saída do audit acima e os logs em $LOG_DIR"
  amarelo "dica: reexecute só o que falhou (--faixa <id> / --seq <T-xxx>)"
  exit 1
}

executar_tudo() {
  evento --tipo inicio --escopo tudo
  iniciar_resumos
  info "logs em: $LOG_DIR"
  info "resumo geral de andamento: a cada 1 min aqui no terminal (e via: onp-spec resumo)"
  executar_seq_T_001 || true
  executar_seq_T_002 || true
  executar_seq_T_003 || true
  executar_seq_T_004 || true
  executar_seq_T_005 || true
  executar_seq_T_006 || true
  executar_seq_T_007 || true
  executar_seq_T_008 || true
  executar_seq_T_009 || true
  encerrar tudo
}

listar() {
  echo "execução: $RUN_ID (feature $FEATURE, branch $BASE_BRANCH)"
  echo "  seq       T-001 (sequencial)"
  echo "  seq       T-002 (sequencial)"
  echo "  seq       T-003 (sequencial)"
  echo "  seq       T-004 (sequencial)"
  echo "  seq       T-005 (sequencial)"
  echo "  seq       T-006 (sequencial)"
  echo "  seq       T-007 (sequencial)"
  echo "  seq       T-008 (sequencial)"
  echo "  seq       T-009 (sequencial)"
  echo
  echo "reexecutar uma faixa:    --faixa <id>"
  echo "reexecutar sequencial:   --seq <T-xxx>"
  echo "só o gate:               --gate"
}

MODO="tudo"
ALVO=""
while [ $# -gt 0 ]; do
  case "$1" in
    --listar) MODO="listar" ;;
    --gate) MODO="gate" ;;
    --sem-gate) COM_GATE=0 ;;
    --faixa) MODO="faixa"; ALVO="${2:-}"; shift ;;
    --seq) MODO="seq"; ALVO="${2:-}"; shift ;;
    -h|--help) sed -n "2,14p" "$0"; exit 0 ;;
    *) vermelho "argumento desconhecido: $1"; sed -n "2,14p" "$0"; exit 2 ;;
  esac
  shift
done

if [ "$MODO" = "listar" ]; then listar; exit 0; fi

preparar_ambiente

case "$MODO" in
  tudo) executar_tudo ;;
  gate) COM_GATE=1; iniciar_resumos; encerrar gate ;;
  faixa)
    case "$ALVO" in
      *) falhar "faixa desconhecida: '$ALVO' — veja as disponíveis com --listar" ;;
    esac ;;
  seq)
    case "$ALVO" in
      T-001) evento --tipo inicio --escopo "seq:T-001"; iniciar_resumos; executar_seq_T_001 || true; encerrar "seq:T-001" ;;
      T-002) evento --tipo inicio --escopo "seq:T-002"; iniciar_resumos; executar_seq_T_002 || true; encerrar "seq:T-002" ;;
      T-003) evento --tipo inicio --escopo "seq:T-003"; iniciar_resumos; executar_seq_T_003 || true; encerrar "seq:T-003" ;;
      T-004) evento --tipo inicio --escopo "seq:T-004"; iniciar_resumos; executar_seq_T_004 || true; encerrar "seq:T-004" ;;
      T-005) evento --tipo inicio --escopo "seq:T-005"; iniciar_resumos; executar_seq_T_005 || true; encerrar "seq:T-005" ;;
      T-006) evento --tipo inicio --escopo "seq:T-006"; iniciar_resumos; executar_seq_T_006 || true; encerrar "seq:T-006" ;;
      T-007) evento --tipo inicio --escopo "seq:T-007"; iniciar_resumos; executar_seq_T_007 || true; encerrar "seq:T-007" ;;
      T-008) evento --tipo inicio --escopo "seq:T-008"; iniciar_resumos; executar_seq_T_008 || true; encerrar "seq:T-008" ;;
      T-009) evento --tipo inicio --escopo "seq:T-009"; iniciar_resumos; executar_seq_T_009 || true; encerrar "seq:T-009" ;;
      *) falhar "tarefa sequencial desconhecida: '$ALVO' — veja as disponíveis com --listar" ;;
    esac ;;
esac
