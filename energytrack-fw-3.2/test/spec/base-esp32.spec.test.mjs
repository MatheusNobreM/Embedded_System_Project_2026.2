// Base ESP32 DevKit (US-007): mapa de pinos e protocolo do PZEM.
import { test } from 'node:test';
import { runHostTest, assertCriterion, assertAllPassed } from './lib/host.mjs';

const pinmap = () => runHostTest('test_pinmap', ['src/hal/pzem_modbus.c']);

test('@spec:AC-019 — mapa de pinos só com o que existe no ESP32 DevKit + PZEM', () => {
  assertCriterion(pinmap(), 'AC-019');
});

test('mapa de pinos e protocolo Modbus do PZEM (herdados do WPS 3.2)', () => {
  assertAllPassed(pinmap());
});
