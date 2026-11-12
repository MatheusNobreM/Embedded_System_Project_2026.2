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
  // Sanidade: a aquisição rodou no ritmo esperado (pzem a 1 Hz).
  if (rt.tasks.pzem && rt.tasks.pzem.jobs < 0.9 * rt.t) problems.push(`pzem com só ${rt.tasks.pzem.jobs} jobs em ${rt.t}s`);

  const pzem = parseLastPzemCounts(log);
  if (!pzem) problems.push('nenhuma linha "PZEM ok=... falha=..." no log');
  else if (pzem.ok === 0 || pzem.ok * 100 < (pzem.ok + pzem.fail) * 95) {
    problems.push(`PZEM com ${pzem.ok} leituras válidas de ${pzem.ok + pzem.fail} (precisa de >= 95%)`);
  }
  if (!/>>> RT PASS <<</.test(log)) problems.push('a linha ">>> RT PASS <<<" não aparece no log');
  return problems;
}

/* ---- bancada com a rede ligada (feature telemetria-wifi, AC-028) ---- */

/** Última linha "rede: ..." do log: { wifi, ip, ok, falha } ou { desligada: motivo }. */
export function parseLastNetLine(log) {
  let last = null;
  for (const line of log.split(/\r?\n/)) {
    const off = line.match(/rede: desligada \((.*)\)/);
    if (off) { last = { desligada: off[1] }; continue; }
    const m = line.match(/rede: wifi=(conectado|desconectado)(?: ip=(\S+))? \| envios ok=(\d+) falha=(\d+)/);
    if (m) last = { wifi: m[1], ip: m[2] ?? '', ok: Number(m[3]), falha: Number(m[4]) };
  }
  return last;
}

/**
 * Leituras gravadas pelo backend de teste (JSON Lines), já conferidas.
 * { registros: [{ dados, recebido_em }], problemas: [texto] }
 */
export function parseReceived(jsonl, problemasDoContrato) {
  const registros = [];
  const problemas = [];
  jsonl.split(/\r?\n/).forEach((linha, i) => {
    if (!linha.trim()) return;
    let r;
    try {
      r = JSON.parse(linha);
    } catch {
      problemas.push(`linha ${i + 1} do arquivo do backend não é JSON`);
      return;
    }
    const fora = problemasDoContrato(r?.dados);
    if (fora.length) problemas.push(`linha ${i + 1} fora do contrato: ${fora.join('; ')}`);
    else registros.push(r);
  });
  return { registros, problemas };
}

/** Envios do último boot da última placa que mandou: seq recomeça quando a placa reinicia. */
function lastRun(registros) {
  const device = registros.at(-1).dados.device;
  const run = [];
  for (const r of registros) {
    if (r.dados.device !== device) continue;
    if (run.length && r.dados.seq <= run.at(-1).dados.seq) run.length = 0;
    run.push(r);
  }
  return run;
}

/**
 * Problemas que impedem o aceite da bancada com rede (lista vazia = aceito):
 * o log serial passa no aceite de tempo real com o Wi-Fi conectado, e o
 * backend de teste recebeu, dentro do contrato, pelo menos minPct% dos envios
 * de uma sequência que cobre pelo menos minSeconds.
 */
export function telemetryBenchProblems(log, jsonl, hardTasks, problemasDoContrato, { minSeconds = 600, minPct = 95 } = {}) {
  const problems = benchProblems(log, hardTasks, minSeconds).map((p) => `serial: ${p}`);

  const net = parseLastNetLine(log);
  if (!net) problems.push('serial: nenhuma linha "rede:" no log (o firmware gravado é de antes da telemetria?)');
  else if (net.desligada !== undefined) problems.push(`serial: a rede estava desligada (${net.desligada}): configure o Wi-Fi e a URL no menuconfig`);
  else {
    if (net.wifi !== 'conectado') problems.push('serial: o último relatório mostra o Wi-Fi desconectado');
    if (net.ok === 0) problems.push('serial: nenhum envio ok ao backend');
  }

  const { registros, problemas } = parseReceived(jsonl, problemasDoContrato);
  problems.push(...problemas.map((p) => `backend: ${p}`));
  if (registros.length === 0) {
    problems.push('backend: o backend de teste não recebeu nenhuma leitura');
    return problems;
  }
  const run = lastRun(registros);
  const first = run[0].dados;
  const last = run.at(-1).dados;
  const expected = last.seq - first.seq + 1;
  const got = new Set(run.map((r) => r.dados.seq)).size;
  if (got * 100 < expected * minPct) {
    problems.push(`backend: recebeu ${got} de ${expected} envios (seq ${first.seq} a ${last.seq}); precisa de >= ${minPct}%`);
  }
  const span = last.uptime_s - first.uptime_s;
  if (span < minSeconds) {
    problems.push(`backend: as leituras cobrem ${span}s (uptime ${first.uptime_s}s a ${last.uptime_s}s); precisa de pelo menos ${minSeconds}s`);
  }
  return problems;
}
