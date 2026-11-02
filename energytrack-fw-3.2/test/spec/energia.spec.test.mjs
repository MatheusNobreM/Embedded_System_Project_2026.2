// Potência e energia (US-003).
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const energy = () => runHostTest('test_energy', ['src/proc/energy.c']);

test('@spec:AC-005 — energia é a integral da potência no tempo', () => {
  assertCriterion(energy(), 'AC-005');
});

test('@spec:AC-006 — intervalo sem leitura é recuperado pelo contador do PZEM', () => {
  assertCriterion(energy(), 'AC-006');
});

test('@spec:AC-007 — o dia vira a cada 24 h', () => {
  assertCriterion(energy(), 'AC-007');
});

test('energia: casos auxiliares', () => {
  assertAllPassed(energy());
});
