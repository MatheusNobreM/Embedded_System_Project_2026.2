// Aquisição (US-001, US-002): quadro do ADC contínuo do ESP32 e conversão do LM35.
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const temp = () => runHostTest('test_temp', ['src/proc/temp_sensor.c', 'src/hal/adc_frame.c']);

test('@spec:AC-003 — bloco de amostras do ADC vira temperatura em °C', () => {
  assertCriterion(temp(), 'AC-003');
});

test('aquisição do LM35: ritmo do bloco e entradas inválidas', () => {
  assertAllPassed(temp());
});
