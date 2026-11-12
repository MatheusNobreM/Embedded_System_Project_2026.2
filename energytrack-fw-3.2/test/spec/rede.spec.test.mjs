// Rede (US-008, US-009): a rede fica isolada no núcleo 0, nada dela grava
// na flash e as credenciais ficam fora do repositório. Confere a
// configuração do ESP-IDF e o código; a prova com a placa funcionando é a
// bancada (AC-028).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { assertCriterion, FW_DIR, runHostTest } from './lib/host.mjs';

const read = (rel) => readFileSync(path.join(FW_DIR, rel), 'utf-8');
const rt = () => runHostTest('test_rt', ['src/rt/rt_monitor.c', 'src/rt/rt_sched.c', 'src/rt/rt_tasks.c']);

/** Linhas que o sdkconfig precisa ter para a rede não atrapalhar o núcleo 1. */
const NETWORK_CONFIG = [
  ['CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y', 'tarefa do Wi-Fi no núcleo 0'],
  ['CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y', 'pilha TCP/IP (lwIP) no núcleo 0'],
  ['# CONFIG_ESP_WIFI_NVS_ENABLED is not set', 'Wi-Fi sem NVS: não grava na flash'],
  ['# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set', 'calibração do rádio não é gravada na flash'],
];

function missing(text, lines) {
  const present = new Set(text.split(/\r?\n/).map((l) => l.trim()));
  return lines.filter(([line]) => !present.has(line)).map(([line, why]) => `${line}  (${why})`);
}

/** Fontes em C do firmware (app/ e src/), relativos a energytrack-fw-3.2/. */
function firmwareSources() {
  return ['app', 'src'].flatMap((dir) => readdirSync(path.join(FW_DIR, dir), { recursive: true })
    .map(String)
    .filter((f) => /\.[ch]$/.test(f))
    .map((f) => path.join(dir, f)));
}

/** Corpo de uma função C de energytrack_main.c (do nome até a próxima função no nível de arquivo). */
function functionBody(source, signature) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, `não achei ${signature} em app/energytrack_main.c`);
  const end = source.indexOf('\n}\n', start);
  return source.slice(start, end > 0 ? end : undefined);
}

test('@spec:AC-026 — a rede fica isolada no núcleo 0 e não grava na flash', () => {
  // A net fica fora da tabela analisada do núcleo 1 (teste em C).
  assertCriterion(rt(), 'AC-026');

  assert.deepEqual(missing(read('sdkconfig.defaults'), NETWORK_CONFIG), [], 'sdkconfig.defaults sem a configuração da rede');

  // O sdkconfig gerado não relê o sdkconfig.defaults: se for de antes da rede,
  // a placa roda com o Wi-Fi gravando na flash e solto entre os núcleos.
  const generated = path.join(FW_DIR, 'sdkconfig.energytrack_fw');
  if (existsSync(generated)) {
    assert.deepEqual(missing(readFileSync(generated, 'utf-8'), NETWORK_CONFIG), [],
      'sdkconfig.energytrack_fw é de antes da rede: renomeie (ou apague) e compile de novo, ' +
      'para ele ser refeito a partir do sdkconfig.defaults (as opções CONFIG_ET_* do Wi-Fi precisam ser preenchidas de novo)');
  }

  const main = read('app/energytrack_main.c');
  assert.match(main, /#define NET_CORE\s+0\b/, 'NET_CORE precisa ser o núcleo 0');
  assert.match(main, /create_task_on_core\(net_task,[^;]*NET_CORE\)/, 'a tarefa net precisa ser criada no NET_CORE');
  // O driver aloca a interrupção do rádio no núcleo de quem o inicia: o Wi-Fi sobe dentro da net.
  assert.match(functionBody(main, 'static void net_task('), /wifi_link_start\(/, 'o Wi-Fi precisa ser iniciado dentro da net_task');
  assert.equal(main.match(/wifi_link_start\(/g).length, 1, 'wifi_link_start só pode ser chamado na net_task');

  assert.match(read('src/hal/wifi_link.c'), /esp_wifi_set_storage\(WIFI_STORAGE_RAM\)/, 'a configuração do Wi-Fi precisa ficar só na RAM');
  const nvsWriters = firmwareSources().filter((f) => /\bnvs_(flash_init|set_\w+|commit)\s*\(/.test(read(f)));
  assert.deepEqual(nvsWriters, [], 'nenhum fonte do firmware pode iniciar ou gravar a NVS');
});

test('@spec:AC-027 — sem rede configurada o firmware avisa o motivo e segue sem Wi-Fi', () => {
  // Credenciais vêm do menuconfig, vazias por padrão.
  const kconfig = read('src/Kconfig.projbuild');
  for (const opt of ['ET_WIFI_SSID', 'ET_WIFI_PASSWORD', 'ET_BACKEND_URL']) {
    const block = kconfig.match(new RegExp(`config ${opt}\\b[\\s\\S]*?(?=\\n\\s*config |\\nendmenu)`));
    assert.ok(block, `${opt} não está em src/Kconfig.projbuild`);
    assert.match(block[0], /default ""/, `${opt} precisa vir vazio por padrão`);
  }
  assert.doesNotMatch(read('sdkconfig.defaults'), /^CONFIG_ET_/m, 'o sdkconfig.defaults é versionado: SSID, senha e URL não podem estar nele');

  // O sdkconfig com os valores fica fora do git.
  const ignored = read('.gitignore').split(/\r?\n/).map((l) => l.trim());
  assert.ok(ignored.includes('sdkconfig.energytrack_fw'), '.gitignore precisa deixar sdkconfig.energytrack_fw de fora');
  const tracked = spawnSync('git', ['ls-files', '--', 'sdkconfig.energytrack_fw', 'sdkconfig.energytrack_selftest'], {
    cwd: FW_DIR, encoding: 'utf-8',
  });
  assert.equal(tracked.status, 0, `git ls-files falhou: ${tracked.stderr}`);
  assert.equal(tracked.stdout.trim(), '', 'um sdkconfig gerado (com as credenciais) está versionado');

  // A rede só sobe com a configuração conferida; senão a net nem é criada.
  const start = functionBody(read('app/energytrack_main.c'), 'static void start_network(');
  assert.match(start, /telemetry_check_config\(CONFIG_ET_WIFI_SSID, CONFIG_ET_WIFI_PASSWORD, CONFIG_ET_BACKEND_URL\)/);
  const bail = start.indexOf('if (net.cfg != TELEM_CFG_OK)');
  const create = start.indexOf('create_task_on_core(net_task');
  assert.ok(bail >= 0 && create > bail && /return;/.test(start.slice(bail, create)),
    'sem configuração válida, start_network precisa sair antes de criar a net');
});
