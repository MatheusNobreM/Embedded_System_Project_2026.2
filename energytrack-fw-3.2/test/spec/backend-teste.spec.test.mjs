// Backend de teste (US-008): sobe tools/backend-teste.mjs numa porta livre,
// manda por HTTP os JSON que o C da placa monta e confere a resposta e o
// arquivo gravado.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { criarServidor, problemasDoContrato, ROTA } from '../../tools/backend-teste.mjs';
import { printedJson, telemetry } from './lib/telemetria.mjs';

async function comServidor(fn) {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'et-backend-'));
  const arquivo = path.join(dir, 'recebidas.jsonl');
  const logs = [];
  const server = criarServidor({ arquivo, log: (l) => logs.push(l) });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    await fn({ base, arquivo, logs, server });
  } finally {
    await new Promise((resolve) => server.close(resolve));
    rmSync(dir, { recursive: true, force: true });
  }
}

const post = (base, corpo) => fetch(`${base}${ROTA}`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: corpo,
});

const linhas = (arquivo) => readFileSync(arquivo, 'utf-8').split('\n').filter(Boolean).map((l) => JSON.parse(l));

test('@spec:AC-025 — o backend de teste recebe e confere o JSON', async () => {
  const result = telemetry();
  const casos = ['leitura', 'sem-leitura', 'anomalias', 'pior-caso'].map((n) => printedJson(result, n));

  await comServidor(async ({ base, arquivo, logs }) => {
    // O JSON da placa é aceito e gravado com a hora de chegada.
    for (const { text } of casos) {
      const r = await post(base, text);
      assert.equal(r.status, 201, `JSON da placa recusado: ${await r.text()}\n${text}`);
    }
    const gravadas = linhas(arquivo);
    assert.equal(gravadas.length, casos.length);
    gravadas.forEach((g, i) => {
      assert.deepEqual(g.dados, casos[i].json);
      assert.ok(!Number.isNaN(Date.parse(g.recebido_em)), `recebido_em inválido: ${g.recebido_em}`);
    });
    const ultima = await fetch(`${base}${ROTA}/ultima`).then((r) => r.json());
    assert.deepEqual(ultima.dados, casos.at(-1).json);

    // Fora do contrato: 400 com a lista de problemas, e nada é gravado.
    const exemplo = casos[0].json;
    const ruins = [
      ['não é JSON', '{"device": "et-1", '],
      ['sem o campo meta', JSON.stringify({ ...exemplo, meta: undefined })],
      ['tensão como texto', JSON.stringify({ ...exemplo, leitura: { ...exemplo.leitura, v: '221.3' } })],
      ['anomalia desconhecida', JSON.stringify({ ...exemplo, anomalias: ['FALTA_DE_LUZ'] })],
      ['campo a mais', JSON.stringify({ ...exemplo, temperatura: 25 })],
    ];
    for (const [caso, corpo] of ruins) {
      const r = await post(base, corpo);
      assert.equal(r.status, 400, `${caso}: deveria ser recusado`);
      const resposta = await r.json();
      assert.ok(Array.isArray(resposta.problemas) && resposta.problemas.length > 0, `${caso}: sem lista de problemas`);
    }
    assert.equal(linhas(arquivo).length, casos.length, 'um corpo recusado foi gravado');
    assert.ok(logs.some((l) => l.includes('RECUSADO')), 'o console do backend não mostrou as recusas');
  });
});

test('backend de teste aponta envios perdidos pelo seq', async () => {
  const { json } = printedJson(telemetry(), 'leitura');
  await comServidor(async ({ base, logs }) => {
    for (const seq of [10, 11, 14]) assert.equal((await post(base, JSON.stringify({ ...json, seq }))).status, 201);
    assert.ok(logs.some((l) => l.includes('faltaram 2 envio(s)')), `logs:\n${logs.join('\n')}`);
  });
});

test('backend de teste: rotas desconhecidas e método errado', async () => {
  await comServidor(async ({ base }) => {
    assert.equal((await fetch(`${base}/api/outra`, { method: 'POST', body: '{}' })).status, 404);
    assert.equal((await fetch(`${base}${ROTA}`)).status, 405);
    assert.equal((await fetch(`${base}${ROTA}/ultima`)).status, 404);
    assert.equal((await fetch(`${base}/`)).status, 200);
  });
});

test('contrato: mensagens de problema dizem o campo', () => {
  const { json } = printedJson(telemetry(), 'leitura');
  assert.deepEqual(problemasDoContrato(json), []);
  assert.deepEqual(problemasDoContrato({ ...json, leitura: null }), []);
  assert.ok(problemasDoContrato({ ...json, seq: 0 }).some((p) => p.startsWith('corpo.seq')));
  assert.ok(problemasDoContrato({ ...json, meta: { ...json.meta, estado: 'BOM' } }).some((p) => p.startsWith('corpo.meta.estado')));
  assert.ok(problemasDoContrato({ ...json, pzem: { ok: 1 } }).some((p) => p.includes('corpo.pzem.falha está faltando')));
  assert.deepEqual(problemasDoContrato([]), ['o corpo precisa ser um objeto JSON']);
});
