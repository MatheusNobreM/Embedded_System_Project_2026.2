// Ponte entre os testes do backend (pytest) e o node:test, para o onp-spec.
//
// O onp-spec prova critérios de aceite pelo TAP do node:test e lê as
// anotações @spec nos títulos. Então: roda o pytest do backend uma vez (com
// os JSON que o código em C da placa monta, gerados pelo teste em C da
// telemetria) e cada critério ganha um teste aqui, que exige ao menos um
// teste do pytest marcado com ele (@pytest.mark.ac) e todos eles passando.
// Teste pulado não conta. Mesmo padrão de energytrack-fw-3.2/test/spec/lib/host.mjs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { printedJson, telemetry } from '../../energytrack-fw-3.2/test/spec/lib/telemetria.mjs';

const BACKEND_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const CASOS_DA_PLACA = ['leitura', 'sem-leitura', 'anomalias', 'pior-caso'];

let cache = null;

/** Roda o pytest do backend uma vez por processo e devolve { casos, saida }. */
function pytest() {
  if (cache) return cache;
  const dir = mkdtempSync(path.join(os.tmpdir(), 'et-backend-api-'));
  try {
    const placa = path.join(dir, 'placa.json');
    const resultados = path.join(dir, 'resultados.json');
    const c = telemetry();
    writeFileSync(placa, JSON.stringify(Object.fromEntries(CASOS_DA_PLACA.map((n) => [n, printedJson(c, n).json]))));

    const run = spawnSync('uv', ['run', '--frozen', 'pytest', '-q', '-p', 'no:cacheprovider'], {
      cwd: BACKEND_DIR,
      encoding: 'utf-8',
      maxBuffer: 64 * 1024 * 1024,
      env: { ...process.env, ET_JSON_PLACA: placa, ET_RESULTADOS: resultados },
    });
    const saida = run.error
      ? `não consegui rodar o uv (${run.error.message}): instale o uv (https://docs.astral.sh/uv/)`
      : `${run.stdout || ''}${run.stderr || ''}`;
    const casos = existsSync(resultados) ? JSON.parse(readFileSync(resultados, 'utf-8')) : [];
    cache = { casos, saida, status: run.status };
    return cache;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function assertCriterio(ac) {
  const { casos, saida } = pytest();
  assert.ok(casos.length > 0, `o pytest do backend não rodou nenhum teste\n\nsaída:\n${saida}`);
  const meus = casos.filter((c) => c.criterios.includes(ac));
  assert.ok(meus.length > 0, `nenhum teste do pytest marcado com @pytest.mark.ac("${ac}")`);
  const ruins = meus.filter((c) => c.resultado !== 'passou');
  assert.equal(
    ruins.length,
    0,
    `testes de ${ac} que não passaram:\n${ruins.map((c) => `  ${c.resultado}: ${c.nodeid}\n${c.erro || ''}`).join('\n')}`,
  );
}

test('@spec:AC-029 — o JSON da placa é aceito e guardado com a hora de chegada', () => assertCriterio('AC-029'));
test('@spec:AC-030 — JSON fora do contrato é recusado e não é guardado', () => assertCriterio('AC-030'));
test('@spec:AC-031 — as leituras sobrevivem a reiniciar o backend, e o banco nasce das migrações', () => assertCriterio('AC-031'));
test('@spec:AC-032 — o app vê quais placas mandaram dados', () => assertCriterio('AC-032'));
test('@spec:AC-033 — o app vê a leitura mais recente de uma placa', () => assertCriterio('AC-033'));
test('@spec:AC-034 — o app vê o histórico de um período, em ordem cronológica', () => assertCriterio('AC-034'));
test('@spec:AC-035 — um comando sobe o backend e mostra a URL para a placa', () => assertCriterio('AC-035'));

// backend-camadas: a reorganização não muda a API. Além do retrato do OpenAPI
// (marcado AC-038), todos os testes da backend-api (AC-029 a AC-035) têm de passar.
test('@spec:AC-038 — o comportamento da API não muda com as camadas', () => {
  assertCriterio('AC-038');
  for (const ac of ['AC-029', 'AC-030', 'AC-031', 'AC-032', 'AC-033', 'AC-034', 'AC-035']) assertCriterio(ac);
});

test('backend: todos os testes do pytest passam', () => {
  const { casos, saida, status } = pytest();
  const ruins = casos.filter((c) => c.resultado === 'falhou');
  assert.equal(ruins.length, 0, `testes com falha:\n${ruins.map((c) => `  ${c.nodeid}`).join('\n')}`);
  assert.equal(status, 0, `o pytest saiu com ${status}\n\nsaída:\n${saida}`);
});
