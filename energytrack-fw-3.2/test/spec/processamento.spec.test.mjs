// Processamento integrado e LED de estado (US-001, US-005).
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const processing = () => runHostTest('test_processing', [
  'src/proc/processing.c', 'src/proc/energy.c', 'src/proc/goal.c', 'src/proc/anomaly.c',
]);

test('@spec:AC-001 — leitura com falha é contada e não estraga o cálculo', () => {
  assertCriterion(processing(), 'AC-001');
});

test('@spec:AC-015 — LED da placa mostra o estado', () => {
  assertCriterion(processing(), 'AC-015');
});

test('processamento: configuração padrão', () => {
  assertAllPassed(processing());
});
