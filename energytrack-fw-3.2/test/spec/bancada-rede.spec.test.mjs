// Bancada com a rede ligada (US-009): 10 min no ESP32 mandando ao backend de
// teste sem estourar deadline. Usa o mesmo log serial do AC-018 (capturado
// com o Wi-Fi ligado) e o arquivo que o backend de teste grava.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { problemasDoContrato } from '../../tools/backend-teste.mjs';
import { parseLastNetLine, parseTaskTable, telemetryBenchProblems } from './lib/bench.mjs';
import { FW_DIR, runHostTest } from './lib/host.mjs';
import { printedJson, telemetry } from './lib/telemetria.mjs';

const BENCH_LOG = path.join(FW_DIR, 'test', 'bench', 'serial-bancada.log');
const RECEIVED = path.join(FW_DIR, 'test', 'bench', 'telemetria-recebida.jsonl');

function hardTasks() {
  const rt = runHostTest('test_rt', ['src/rt/rt_monitor.c', 'src/rt/rt_sched.c', 'src/rt/rt_tasks.c']);
  assert.ok(!rt.buildError, `falha ao compilar test_rt:\n${rt.buildError}`);
  return parseTaskTable(rt.output).filter((t) => t.hard).map((t) => t.name);
}

test('@spec:AC-028 — 10 minutos na bancada enviando ao backend, sem estouro de deadline', () => {
  const howTo = 'veja test/bench/LEIAME.md: configure o Wi-Fi e a URL no menuconfig, grave o energytrack_fw, ' +
    'suba o backend de teste (node tools/backend-teste.mjs) e capture o log com tools/capturar-serial.ps1';
  assert.ok(existsSync(BENCH_LOG), `log serial da bancada ainda não capturado (test/bench/serial-bancada.log): ${howTo}`);
  assert.ok(existsSync(RECEIVED), `o backend de teste ainda não gravou test/bench/telemetria-recebida.jsonl: ${howTo}`);
  const problems = telemetryBenchProblems(readFileSync(BENCH_LOG, 'utf-8'), readFileSync(RECEIVED, 'utf-8'),
    hardTasks(), problemasDoContrato);
  assert.deepEqual(problems, [], `a bancada com rede não fechou:\n  ${problems.join('\n  ')}`);
});

/* ---- o próprio leitor da bancada com rede, testado com dados sintéticos ---- */

const NET_OK = '  rede: wifi=conectado ip=192.168.0.23 | envios ok=130 falha=2 (wifi=2 rede=0 http=0 json=0) | ultimo=HTTP 201 | seq=132 | envio max=85ms';

function serialLog({ net = NET_OK, t = 660 } = {}) {
  return [
    `[  ${t}s] PZEM ok=${t} falha=0 | V=220.1 V I=1.000 A P=220.0 W f=60.0 Hz FP=1.00`,
    net,
    `  rt t=${t}s proc:jobs=${t},miss=0,rmax=300us,d=50000us pzem:jobs=${t},miss=0,rmax=48000us,d=500000us`,
    '  >>> RT PASS <<< 600 s sem estouro de deadline',
  ].join('\n');
}

/** Registros como o backend de teste grava, a partir do JSON montado pelo C da placa. */
function received(seqs, { device, firstUptime = 10 } = {}) {
  const { json } = printedJson(telemetry(), 'leitura');
  return seqs.map((seq) => JSON.stringify({
    recebido_em: '2026-11-12T22:00:00.000Z',
    origem: '192.168.0.23',
    dados: { ...json, device: device ?? json.device, seq, uptime_s: firstUptime + (seq - seqs[0]) * 5 },
  })).join('\n');
}

const range = (from, to) => Array.from({ length: to - from + 1 }, (_, i) => from + i);
const HARD = ['proc', 'pzem'];

test('leitor da bancada com rede aceita uma sessão boa', () => {
  assert.deepEqual(telemetryBenchProblems(serialLog(), received(range(1, 130)), HARD, problemasDoContrato), []);
});

test('leitor da bancada com rede recusa as sessões ruins', () => {
  const has = (problems, text) => problems.some((p) => p.includes(text));
  const check = (opts) => telemetryBenchProblems(opts.log ?? serialLog(), opts.jsonl ?? received(range(1, 130)), HARD, problemasDoContrato);

  const gaps = range(1, 130).filter((s) => s % 10 !== 0);   /* perdeu 10% */
  assert.ok(has(check({ jsonl: received(gaps) }), '>= 95%'));
  assert.ok(has(check({ jsonl: received(range(1, 100)) }), 'precisa de pelo menos 600s'));
  assert.ok(has(check({ jsonl: '' }), 'não recebeu nenhuma leitura'));
  assert.ok(has(check({ jsonl: `${received(range(1, 130))}\n{"dados":{"seq":1}}` }), 'fora do contrato'));
  assert.ok(has(check({ log: serialLog({ net: '  rede: desligada (sem SSID)' }) }), 'a rede estava desligada'));
  assert.ok(has(check({ log: serialLog({ net: NET_OK.replace('wifi=conectado ip=192.168.0.23', 'wifi=desconectado') }) }), 'Wi-Fi desconectado'));
  assert.ok(has(check({ log: serialLog({ net: '' }) }), 'nenhuma linha "rede:"'));
  assert.ok(has(check({ log: serialLog({ t: 300 }) }), 'serial:'));

  // A placa reiniciou no meio: vale só o último boot.
  const reboot = `${received(range(1, 40))}\n${received(range(1, 130), { firstUptime: 8 })}`;
  assert.deepEqual(check({ jsonl: reboot }), []);
});

test('leitor da linha "rede:" do relatório', () => {
  assert.deepEqual(parseLastNetLine(NET_OK), { wifi: 'conectado', ip: '192.168.0.23', ok: 130, falha: 2 });
  assert.deepEqual(parseLastNetLine('  rede: desligada (sem URL do backend)'), { desligada: 'sem URL do backend' });
  const rede = telemetry().output.split(/\r?\n/).find((l) => l.startsWith('# rede-linha: '));
  assert.ok(rede, 'o teste em C não imprimiu a linha "rede:" de exemplo');
  assert.deepEqual(parseLastNetLine(rede), { wifi: 'conectado', ip: '192.168.0.23', ok: 118, falha: 2 });
});
