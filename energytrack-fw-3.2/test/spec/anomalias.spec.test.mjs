// Detecção de anomalias (US-001, US-002, US-005).
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const anomaly = () => runHostTest('test_anomaly', ['src/proc/anomaly.c']);

test('@spec:AC-002 — sensor sem resposta vira alerta depois de 5 falhas seguidas', () => {
  assertCriterion(anomaly(), 'AC-002');
});

test('@spec:AC-004 — superaquecimento da placa vira alerta', () => {
  assertCriterion(anomaly(), 'AC-004');
});

test('@spec:AC-011 — tensão fora da faixa adequada do PRODIST (rede de 220 V)', () => {
  assertCriterion(anomaly(), 'AC-011');
});

test('@spec:AC-012 — frequência fora de 59,5–60,5 Hz', () => {
  assertCriterion(anomaly(), 'AC-012');
});

test('@spec:AC-013 — fator de potência baixo com carga relevante', () => {
  assertCriterion(anomaly(), 'AC-013');
});

test('@spec:AC-014 — salto de potência atípico', () => {
  assertCriterion(anomaly(), 'AC-014');
});

test('anomalias: todos os casos', () => {
  assertAllPassed(anomaly());
});
