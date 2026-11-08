// Ponte entre os testes em C (saída TAP, ver test/host/tap.h) e o node:test.
//
// O onp-spec só lê anotações @spec em arquivos de texto como .mjs, e prova
// critérios de aceite pelo TAP do node:test. Então cada critério ganha um
// teste aqui, que compila o teste em C com gcc, roda o binário e exige que
// todos os casos marcados com aquele critério tenham passado.

import { spawnSync } from 'node:child_process';
import { mkdirSync, rmSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

export const FW_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const BUILD_DIR = path.join(FW_DIR, 'build-host');

const CFLAGS = [
  '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
  '-Iinclude', '-Isrc/hal', '-Isrc/proc', '-Isrc/rt', '-Itest/host',
];

const RE_CASE = /^(not )?ok\s+\d+\s+-\s+(.*)$/;
const cache = new Map();

/**
 * Compila test/host/<name>.c com as fontes dadas (caminhos relativos a
 * energytrack-fw-3.2/) e roda o binário. Resultado em cache por processo.
 */
export function runHostTest(name, sources = []) {
  if (cache.has(name)) return cache.get(name);

  mkdirSync(BUILD_DIR, { recursive: true });
  // Um executável por processo: o node:test roda os arquivos em paralelo, e
  // dois deles compilam o mesmo teste (test_rt). No Windows, um .exe em uso
  // trava o link do outro.
  const base = `${name}.${process.pid}`;
  const exe = path.join(BUILD_DIR, process.platform === 'win32' ? `${base}.exe` : base);
  const build = spawnSync('gcc', [...CFLAGS, `test/host/${name}.c`, ...sources, '-o', exe], {
    cwd: FW_DIR,
    encoding: 'utf-8',
  });

  let result;
  if (build.error || build.status !== 0) {
    result = { buildError: build.error ? String(build.error) : `${build.stdout}${build.stderr}`, cases: [], output: '' };
  } else {
    const run = spawnSync(exe, [], { cwd: FW_DIR, encoding: 'utf-8' });
    const output = `${run.stdout || ''}${run.stderr || ''}`;
    const cases = [];
    for (const line of output.split(/\r?\n/)) {
      const m = line.match(RE_CASE);
      if (m) cases.push({ ok: !m[1], name: m[2].trim() });
    }
    result = { buildError: null, cases, output, status: run.status };
    rmSync(exe, { force: true });
  }
  cache.set(name, result);
  return result;
}

function assertBuilt(result) {
  assert.ok(!result.buildError, `falha ao compilar o teste em C:\n${result.buildError}`);
}

/** Exige ao menos um caso em C marcado com o critério e nenhum deles falhando. */
export function assertCriterion(result, acId) {
  assertBuilt(result);
  const re = new RegExp(`\\b${acId}\\b`);
  const cases = result.cases.filter((c) => re.test(c.name));
  assert.ok(cases.length > 0, `nenhum caso em C marcado com ${acId}\n\nsaída:\n${result.output}`);
  const failed = cases.filter((c) => !c.ok);
  assert.equal(failed.length, 0, `casos de ${acId} com falha:\n${failed.map((c) => `  ${c.name}`).join('\n')}\n\nsaída:\n${result.output}`);
}

/** Exige que todos os casos do binário tenham passado (e que ele termine com 0). */
export function assertAllPassed(result) {
  assertBuilt(result);
  assert.ok(result.cases.length > 0, `nenhum caso executado\n\nsaída:\n${result.output}`);
  const failed = result.cases.filter((c) => !c.ok);
  assert.equal(failed.length, 0, `casos com falha:\n${failed.map((c) => `  ${c.name}`).join('\n')}\n\nsaída:\n${result.output}`);
  assert.equal(result.status, 0, `o binário terminou com código ${result.status}`);
}
