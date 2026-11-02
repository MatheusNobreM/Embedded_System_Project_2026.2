// Leitura do log serial da bancada e da tabela de tarefas impressa pelo
// teste em C (linhas "# task: nome,prio,periodo_us,deadline_us,orcamento_us,suspensao_us,hard|soft").

/** Tabela de tarefas do firmware, a partir da saída de test_rt. */
export function parseTaskTable(output) {
  const tasks = [];
  for (const line of output.split(/\r?\n/)) {
    const m = line.match(/^# task: (\w+),(\d+),(\d+),(\d+),\d+,\d+,(hard|soft)$/);
    if (!m) continue;
    tasks.push({ name: m[1], priority: Number(m[2]), periodUs: Number(m[3]), deadlineUs: Number(m[4]), hard: m[5] === 'hard' });
  }
  return tasks;
}

/** "100 ms" | "1 s" | "50 µs" -> microssegundos. */
export function durationToUs(text) {
  const m = text.trim().match(/^(\d+(?:[.,]\d+)?)\s*(µs|us|ms|s)$/);
  if (!m) return NaN;
  const value = Number(m[1].replace(',', '.'));
  const scale = { 'µs': 1, us: 1, ms: 1000, s: 1000000 }[m[2]];
  return Math.round(value * scale);
}

/** Linhas da tabela "| `nome` | prio | período | deadline | ..." do diagrama. */
export function parseDiagramTable(markdown) {
  const rows = [];
  for (const line of markdown.split(/\r?\n/)) {
    const m = line.match(/^\|\s*`(\w+)`\s*\|\s*(\d+)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|/);
    if (!m) continue;
    rows.push({ name: m[1], priority: Number(m[2]), periodUs: durationToUs(m[3]), deadlineUs: durationToUs(m[4]) });
  }
  return rows;
}

/** Último relatório de tempo real do log: { t, tasks: { nome: { jobs, miss, rmax, d } } }. */
export function parseLastRtLine(log) {
  let last = null;
  for (const line of log.split(/\r?\n/)) {
    const m = line.match(/rt t=(\d+)s (.*)$/);
    if (!m) continue;
    const tasks = {};
    for (const e of m[2].matchAll(/(\w+):jobs=(\d+),miss=(\d+),rmax=(\d+)us,d=(\d+)us/g)) {
      tasks[e[1]] = { jobs: Number(e[2]), miss: Number(e[3]), rmax: Number(e[4]), d: Number(e[5]) };
    }
    last = { t: Number(m[1]), tasks };
  }
  return last;
}

/** Últimos contadores do PZEM no log ("PZEM ok=N falha=M"). */
export function parseLastPzemCounts(log) {
  let last = null;
  for (const m of log.matchAll(/PZEM ok=(\d+) falha=(\d+)/g)) last = { ok: Number(m[1]), fail: Number(m[2]) };
  return last;
}

/**
 * Problemas que impedem o aceite de bancada (lista vazia = aceito).
 * hardTasks: nomes das tarefas do critério de aceite.
 */
export function benchProblems(log, hardTasks, minSeconds = 600) {
  const problems = [];
  const rt = parseLastRtLine(log);
  if (!rt) return ['nenhuma linha "rt t=..." no log'];
  if (rt.t < minSeconds) problems.push(`o último relatório é de t=${rt.t}s; precisa de pelo menos ${minSeconds}s`);
  for (const name of hardTasks) {
    const s = rt.tasks[name];
    if (!s) { problems.push(`tarefa ${name} ausente da linha rt`); continue; }
    if (s.jobs === 0) problems.push(`${name} não executou nenhum job`);
    if (s.miss !== 0) problems.push(`${name} teve ${s.miss} estouro(s) de deadline`);
    if (s.rmax >= s.d) problems.push(`${name}: resposta máxima ${s.rmax}us >= deadline ${s.d}us`);
  }
  // Sanidade: a aquisição rodou no ritmo esperado (adc a 10 Hz, pzem a 1 Hz).
  if (rt.tasks.adc && rt.tasks.adc.jobs < 0.9 * rt.t * 10) problems.push(`adc com só ${rt.tasks.adc.jobs} jobs em ${rt.t}s`);
  if (rt.tasks.pzem && rt.tasks.pzem.jobs < 0.9 * rt.t) problems.push(`pzem com só ${rt.tasks.pzem.jobs} jobs em ${rt.t}s`);

  const pzem = parseLastPzemCounts(log);
  if (!pzem) problems.push('nenhuma linha "PZEM ok=... falha=..." no log');
  else if (pzem.ok === 0 || pzem.ok * 100 < (pzem.ok + pzem.fail) * 95) {
    problems.push(`PZEM com ${pzem.ok} leituras válidas de ${pzem.ok + pzem.fail} (precisa de >= 95%)`);
  }
  if (!/>>> RT PASS <<</.test(log)) problems.push('a linha ">>> RT PASS <<<" não aparece no log');
  return problems;
}
