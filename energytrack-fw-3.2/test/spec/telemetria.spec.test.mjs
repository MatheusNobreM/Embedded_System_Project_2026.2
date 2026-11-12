// Telemetria (US-008, US-009): o JSON que a placa manda ao backend, a
// contagem dos envios e a linha "rede:" do relatório. O C monta; aqui o
// JSON.parse prova que o texto é JSON válido e o contrato é conferido campo
// a campo.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

export const telemetry = () => runHostTest('test_telemetry', [
  'src/proc/telemetry.c', 'src/proc/anomaly.c', 'src/proc/goal.c',
]);

/** JSON impresso pelo teste em C como "# json-<caso>: {...}". */
export function printedJson(result, name) {
  const prefix = `# json-${name}: `;
  const line = result.output.split(/\r?\n/).find((l) => l.startsWith(prefix));
  assert.ok(line, `o teste em C não imprimiu o JSON "${name}"\n\nsaída:\n${result.output}`);
  const text = line.slice(prefix.length);
  try {
    return { text, json: JSON.parse(text) };
  } catch (err) {
    assert.fail(`"${name}" não é JSON válido (${err.message}):\n${text}`);
  }
}

const CONTRACT_KEYS = ['device', 'seq', 'uptime_s', 'leitura', 'energia', 'meta', 'anomalias', 'tensao_faixa', 'saltos', 'pzem'];

test('@spec:AC-022 — o JSON enviado segue o contrato', () => {
  const result = telemetry();
  assertCriterion(result, 'AC-022');

  const { json } = printedJson(result, 'leitura');
  assert.deepEqual(json, {
    device: 'et-a4cf12345678',
    seq: 121,
    uptime_s: 602,
    leitura: { v: 221.3, i: 1.234, p: 271.9, f: 60.0, fp: 0.98, energia_pzem_wh: 102 },
    energia: { dia: 1, dia_wh: 45.312, anterior_wh: 0, total_wh: 45.312, pmed_w: 271.5, lacunas_recuperadas: 0 },
    meta: { estado: 'OK', uso_pct: 0, projecao_wh: 6516, alerta_potencia: false },
    anomalias: [],
    tensao_faixa: 'adequada',
    saltos: 0,
    pzem: { ok: 601, falha: 0 },
  });
  assert.deepEqual(Object.keys(json), CONTRACT_KEYS, 'campos fora da ordem ou faltando');

  const anomalias = printedJson(result, 'anomalias').json;
  assert.deepEqual(anomalias.anomalias, ['SUBTENSAO', 'FREQUENCIA']);
  assert.equal(anomalias.tensao_faixa, 'precaria');
  assert.equal(anomalias.meta.estado, 'ATENCAO');
  assert.equal(anomalias.meta.alerta_potencia, true);

  const worst = printedJson(result, 'pior-caso').json;
  assert.deepEqual(Object.keys(worst), CONTRACT_KEYS);
  assert.equal(worst.anomalias.length, 6, 'pior caso deveria ter os 6 alertas');
});

test('@spec:AC-023 — sem leitura do PZEM, o campo leitura vai nulo', () => {
  const result = telemetry();
  assertCriterion(result, 'AC-023');
  const { json } = printedJson(result, 'sem-leitura');
  assert.equal(json.leitura, null);
  assert.deepEqual(Object.keys(json), CONTRACT_KEYS, 'os demais campos do contrato continuam presentes');
  assert.deepEqual(json.anomalias, ['SENSOR_SEM_RESPOSTA']);
  assert.deepEqual(json.pzem, { ok: 0, falha: 7 });
});

test('@spec:AC-024 — cada envio é contado e aparece no relatório serial', () => {
  assertCriterion(telemetry(), 'AC-024');
});

test('@spec:AC-027 — sem rede configurada o firmware avisa o motivo e segue sem Wi-Fi', () => {
  assertCriterion(telemetry(), 'AC-027');
});

test('telemetria: todos os casos do teste em C', () => {
  assertAllPassed(telemetry());
});
