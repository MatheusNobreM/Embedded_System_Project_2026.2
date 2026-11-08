// Entregáveis do WPS 3.1 (US-006, US-007): diagrama fiel ao código e prova de bancada.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { FW_DIR, runHostTest } from './lib/host.mjs';
import { benchProblems, parseDiagramTable, parseLastRtLine, parseTaskTable } from './lib/bench.mjs';

const DIAGRAM = path.join(FW_DIR, 'docs', 'diagrama-tarefas.md');
const BENCH_LOG = path.join(FW_DIR, 'test', 'bench', 'serial-bancada.log');

const rtOutput = () => runHostTest('test_rt', ['src/rt/rt_monitor.c', 'src/rt/rt_sched.c', 'src/rt/rt_tasks.c']);

function firmwareTasks() {
  const result = rtOutput();
  assert.ok(!result.buildError, `falha ao compilar test_rt:\n${result.buildError}`);
  const tasks = parseTaskTable(result.output);
  assert.ok(tasks.length > 0, 'test_rt não imprimiu a tabela de tarefas');
  return tasks;
}

test('@spec:AC-021 — diagrama de tarefas confere com o código', () => {
  const tasks = firmwareTasks();
  const rows = parseDiagramTable(readFileSync(DIAGRAM, 'utf-8'));
  const byName = new Map(rows.map((r) => [r.name, r]));

  for (const t of tasks) {
    const row = byName.get(t.name);
    assert.ok(row, `a tarefa ${t.name} não está na tabela do diagrama`);
    assert.equal(row.priority, t.priority, `${t.name}: prioridade ${row.priority} no diagrama, ${t.priority} no código`);
    assert.equal(row.periodUs, t.periodUs, `${t.name}: período ${row.periodUs}us no diagrama, ${t.periodUs}us no código`);
    assert.equal(row.deadlineUs, t.deadlineUs, `${t.name}: deadline ${row.deadlineUs}us no diagrama, ${t.deadlineUs}us no código`);
  }
  const extra = rows.filter((r) => !tasks.some((t) => t.name === r.name)).map((r) => r.name);
  assert.deepEqual(extra, [], `tarefas no diagrama que não existem no código: ${extra.join(', ')}`);
});

test('@spec:AC-018 — 10 minutos na bancada sem estouro de deadline', () => {
  assert.ok(existsSync(BENCH_LOG),
    'log da bancada ainda não capturado: grave o energytrack_fw no ESP32 DevKit (pio run -e energytrack_fw -t upload), ' +
    'ligue o PZEM à rede com carga pelo transformador de corrente e rode ' +
    'tools/capturar-serial.ps1 (gera test/bench/serial-bancada.log)');
  const hard = firmwareTasks().filter((t) => t.hard).map((t) => t.name);
  const problems = benchProblems(readFileSync(BENCH_LOG, 'utf-8'), hard);
  assert.deepEqual(problems, [], `a bancada não fechou:\n  ${problems.join('\n  ')}`);
});

/* ---- o próprio leitor do log, testado com logs sintéticos ---- */

function syntheticLog({ t = 600, miss = 0, ok = 600, fail = 0, pass = true } = {}) {
  return [
    `[  ${t}s] PZEM ok=${ok} falha=${fail} | V=220.1 V I=1.000 A P=220.0 W f=60.0 Hz FP=1.00`,
    `  rt t=${t}s proc:jobs=${t},miss=${miss},rmax=300us,d=50000us ` +
      `pzem:jobs=${t},miss=0,rmax=48000us,d=500000us status:jobs=${t * 20},miss=0,rmax=90us,d=50000us ` +
      `report:jobs=${t / 2},miss=0,rmax=9000us,d=2000000us`,
    pass ? '  >>> RT PASS <<< 600 s sem estouro de deadline' : '  RT: OK ate agora',
  ].join('\n');
}

test('leitor do log de bancada entende a linha que o firmware imprime', () => {
  const sample = rtOutput().output.split(/\r?\n/).find((l) => l.startsWith('# rtline: '));
  assert.ok(sample, 'test_rt não imprimiu a linha de exemplo');
  const rt = parseLastRtLine(sample);
  assert.deepEqual(rt, { t: 600, tasks: { proc: { jobs: 1, miss: 0, rmax: 212, d: 50000 } } });
});

test('leitor do log de bancada aceita um log bom e recusa os ruins', () => {
  const hard = ['proc', 'pzem'];
  assert.deepEqual(benchProblems(syntheticLog(), hard), []);
  assert.ok(benchProblems(syntheticLog({ miss: 1 }), hard).some((p) => p.includes('estouro')));
  assert.ok(benchProblems(syntheticLog({ t: 300 }), hard).some((p) => p.includes('600s')));
  assert.ok(benchProblems(syntheticLog({ ok: 500, fail: 100 }), hard).some((p) => p.includes('95%')));
  assert.ok(benchProblems(syntheticLog({ pass: false }), hard).some((p) => p.includes('RT PASS')));
});
