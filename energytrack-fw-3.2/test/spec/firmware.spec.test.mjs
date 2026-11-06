// Firmware (US-006, US-007): os dois ambientes compilam para o ESP32 DevKit
// sem avisos nos fontes do projeto, com o ESP-IDF que o PlatformIO instala em
// ~/.platformio (plataforma espressif32 fixada no platformio.ini).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, readdirSync, rmSync, statSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { FW_DIR } from './lib/host.mjs';

const ENVS = ['energytrack_fw', 'energytrack_selftest'];
const PIO_BUILD = path.join(FW_DIR, '.pio', 'build');
const EXE = process.platform === 'win32' ? '.exe' : '';

/** pio do PlatformIO IDE (penv) ou, se não houver, o do PATH. */
function pioCommand() {
  const penv = path.join(os.homedir(), '.platformio', 'penv', process.platform === 'win32' ? 'Scripts' : 'bin', `pio${EXE}`);
  return existsSync(penv) ? penv : 'pio';
}

function run(cmd, args) {
  const r = spawnSync(cmd, args, { cwd: FW_DIR, encoding: 'utf-8', maxBuffer: 256 * 1024 * 1024 });
  return { status: r.status, output: `${r.stdout ?? ''}${r.stderr ?? ''}${r.error ? String(r.error) : ''}` };
}

/** Avisos/erros cujo arquivo é do projeto (app/, src/, include/), não do ESP-IDF. */
function projectDiagnostics(output) {
  const root = FW_DIR.split(path.sep).join('/').toLowerCase();
  return output
    .split(/\r?\n/)
    .filter((line) => /: (warning|error):/.test(line))
    .filter((line) => {
      const l = line.split('\\').join('/').toLowerCase();
      return ['app/', 'src/', 'include/'].some((d) => l.includes(`${root}/${d}`) || l.startsWith(d) || l.startsWith(`../${d}`));
    });
}

/** Objetos dos fontes do projeto (o PlatformIO compila o componente src/ por conta própria). */
function removeProjectObjects(envDir) {
  for (const sub of ['src', 'app']) {
    rmSync(path.join(envDir, sub), { recursive: true, force: true });
  }
}

test('@spec:AC-020 — firmware compila sem avisos', () => {
  const pio = pioCommand();
  const version = run(pio, ['--version']);
  assert.equal(version.status, 0, `PlatformIO não encontrado (${pio}) — instale a extensão PlatformIO IDE:\n${version.output}`);

  // Apaga só os objetos dos fontes do projeto: eles recompilam e os avisos reaparecem.
  for (const env of ENVS) removeProjectObjects(path.join(PIO_BUILD, env));

  const started = Date.now();
  const build = run(pio, ['run', ...ENVS.flatMap((e) => ['-e', e])]);
  assert.equal(build.status, 0, `compilação falhou:\n${build.output.slice(-8000)}`);

  const diags = projectDiagnostics(build.output);
  assert.equal(diags.length, 0, `avisos nos fontes do projeto:\n${diags.join('\n')}`);

  for (const env of ENVS) {
    const bin = path.join(PIO_BUILD, env, 'firmware.bin');
    assert.ok(existsSync(bin), `${env}: firmware.bin não foi gerado`);
    assert.ok(statSync(bin).mtimeMs >= started - 2000, `${env}: firmware.bin não foi regravado nesta compilação`);
    const objs = readdirSync(path.join(PIO_BUILD, env), { recursive: true }).filter((f) => /energytrack_main|selftest_main/.test(String(f)));
    assert.ok(objs.length > 0, `${env}: nenhum objeto do main do projeto — o build não passou pelos fontes do projeto?`);
  }
});
