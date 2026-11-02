// Aquisição (US-001, US-002): conversão do bloco de ADC da temperatura interna.
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const temp = () => runHostTest('test_temp', ['src/proc/temp_sensor.c']);

test('@spec:AC-003 — bloco de amostras do ADC vira temperatura em °C', () => {
  assertCriterion(temp(), 'AC-003');
});

test('conversão de temperatura: entradas inválidas', () => {
  assertAllPassed(temp());
});
