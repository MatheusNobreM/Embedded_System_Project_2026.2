// Firmware (US-006, US-007): os dois alvos compilam para a Pico W sem avisos
// nos fontes do projeto, com as ferramentas que a extensão Raspberry Pi Pico
// instala em ~/.pico-sdk.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { FW_DIR } from './lib/host.mjs';

const BUILD_DIR = path.join(FW_DIR, 'build');
const TARGETS = ['energytrack_fw', 'energytrack_selftest'];
const EXE = process.platform === 'win32' ? '.exe' : '';

function newestVersionDir(dir) {
  try {
    return readdirSync(dir).filter((d) => /^v?\d/.test(d)).sort().reverse()[0];
  } catch {
    return undefined;
  }
}

function picoTools() {
  const root = path.join(os.homedir(), '.pico-sdk');
  const cmakeLists = readFileSync(path.join(FW_DIR, 'CMakeLists.txt'), 'utf-8');
  const toolchain = cmakeLists.match(/set\(toolchainVersion\s+(\S+)\)/)?.[1] ?? '';
  const cmakeVer = newestVersionDir(path.join(root, 'cmake')) ?? '';
  const ninjaVer = newestVersionDir(path.join(root, 'ninja')) ?? '';
  return {
    cmake: path.join(root, 'cmake', cmakeVer, 'bin', `cmake${EXE}`),
    ninja: path.join(root, 'ninja', ninjaVer, `ninja${EXE}`),
    toolchainBin: path.join(root, 'toolchain', toolchain, 'bin'),
  };
}

function envWithPath(dirs) {
  const env = { ...process.env };
  const key = Object.keys(env).find((k) => k.toLowerCase() === 'path') ?? 'PATH';
  env[key] = [...dirs, env[key] ?? ''].join(path.delimiter);
  return env;
}

function run(cmd, args, env) {
  const r = spawnSync(cmd, args, { cwd: FW_DIR, env, encoding: 'utf-8', maxBuffer: 64 * 1024 * 1024 });
  return { status: r.status, output: `${r.stdout ?? ''}${r.stderr ?? ''}${r.error ? String(r.error) : ''}` };
}

/** Avisos/erros cujo arquivo é do projeto (app/, src/, include/, config/), não do SDK. */
function projectDiagnostics(output) {
  const root = FW_DIR.split(path.sep).join('/').toLowerCase();
  return output
    .split(/\r?\n/)
    .filter((line) => /: (warning|error):/.test(line))
    .filter((line) => {
      const l = line.split('\\').join('/').toLowerCase();
      return ['app/', 'src/', 'include/', 'config/'].some((d) => l.includes(`${root}/${d}`) || l.startsWith(d) || l.startsWith(`../${d}`));
    });
}

test('@spec:AC-020 — firmware compila sem avisos', () => {
  const tools = picoTools();
  for (const p of [tools.cmake, tools.ninja, tools.toolchainBin]) {
    assert.ok(existsSync(p), `não achei ${p} — importe o projeto pela extensão Raspberry Pi Pico (SDK 2.1.1)`);
  }
  const env = envWithPath([tools.toolchainBin, path.dirname(tools.ninja), path.dirname(tools.cmake)]);

  if (!existsSync(path.join(BUILD_DIR, 'CMakeCache.txt'))) {
    const cfg = run(tools.cmake, ['-S', FW_DIR, '-B', BUILD_DIR, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
      `-DCMAKE_MAKE_PROGRAM=${tools.ninja}`], env);
    assert.equal(cfg.status, 0, `configuração do CMake falhou:\n${cfg.output}`);
  }

  // Apaga só os objetos dos fontes do projeto: eles recompilam e os avisos reaparecem.
  for (const target of TARGETS) {
    for (const sub of ['app', 'src']) {
      rmSync(path.join(BUILD_DIR, 'CMakeFiles', `${target}.dir`, sub), { recursive: true, force: true });
    }
  }

  const started = Date.now();
  const build = run(tools.cmake, ['--build', BUILD_DIR], env);
  assert.equal(build.status, 0, `compilação falhou:\n${build.output.slice(-6000)}`);

  const diags = projectDiagnostics(build.output);
  assert.equal(diags.length, 0, `avisos nos fontes do projeto:\n${diags.join('\n')}`);

  for (const target of TARGETS) {
    const uf2 = path.join(BUILD_DIR, `${target}.uf2`);
    assert.ok(existsSync(uf2), `${target}.uf2 não foi gerado`);
    assert.ok(statSync(uf2).mtimeMs >= started - 2000, `${target}.uf2 não foi regravado nesta compilação`);
  }
});
