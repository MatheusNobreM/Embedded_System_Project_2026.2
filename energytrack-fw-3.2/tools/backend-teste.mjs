#!/usr/bin/env node
// Backend de teste do EnergyTrack: recebe o POST que o ESP32 manda pelo
// Wi-Fi, confere o contrato do JSON e grava cada leitura aceita.
//
// Uso (na pasta energytrack-fw-3.2):
//   node tools/backend-teste.mjs                  porta 8080
//   node tools/backend-teste.mjs --porta 9000
//   node tools/backend-teste.mjs --arquivo outro.jsonl   (não mexe na evidência da bancada)
//
// Na placa, configure no menuconfig a URL que este script imprime ao subir
// (http://<ip-deste-pc>:8080/api/leituras). O PC e o ESP32 precisam estar na
// mesma rede; no Windows, permita o Node no firewall quando ele perguntar.
//
// Rotas:
//   POST /api/leituras         201 = aceito e gravado · 400 = fora do contrato
//   GET  /api/leituras/ultima  última leitura aceita (ou 404)
//   GET  /                     resumo em texto
//
// Cada leitura aceita vira uma linha de test/bench/telemetria-recebida.jsonl
// ({"recebido_em", "origem", "dados"}). O arquivo é zerado a cada execução:
// ele é a evidência da bancada da sessão atual (critério de aceite AC-028).
// Sem dependências: só Node (>= 20).
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { appendFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const FW_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const ARQUIVO_PADRAO = path.join(FW_DIR, 'test', 'bench', 'telemetria-recebida.jsonl');
export const ROTA = '/api/leituras';
const LIMITE_CORPO = 16 * 1024;

/* ------------------------------------------------------------ contrato -- */
export const CAMPOS = ['device', 'seq', 'uptime_s', 'leitura', 'energia', 'meta', 'anomalias', 'tensao_faixa', 'saltos', 'pzem'];
const ESTADOS_META = ['OK', 'PROJECAO_ACIMA', 'ATENCAO', 'ESTOURADA'];
const ANOMALIAS = ['SUBTENSAO', 'SOBRETENSAO', 'FREQUENCIA', 'FP_BAIXO', 'SALTO_POTENCIA', 'SENSOR_SEM_RESPOSTA'];
const FAIXAS = ['adequada', 'precaria', 'critica'];

const ehObjeto = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);

/** Confere o objeto com os campos exatos; devolve os problemas encontrados. */
function conferirCampos(obj, nome, campos, regras, problemas) {
  if (!ehObjeto(obj)) {
    problemas.push(`${nome} precisa ser um objeto`);
    return;
  }
  for (const c of campos) if (!(c in obj)) problemas.push(`${nome}.${c} está faltando`);
  for (const c of Object.keys(obj)) if (!campos.includes(c)) problemas.push(`${nome}.${c} não faz parte do contrato`);
  for (const [c, regra] of Object.entries(regras)) if (c in obj) regra(obj[c], `${nome}.${c}`, problemas);
}

const numero = (v, nome, p) => {
  if (typeof v !== 'number' || !Number.isFinite(v) || v < 0) p.push(`${nome} precisa ser um número >= 0 (veio ${JSON.stringify(v)})`);
};
const inteiro = (v, nome, p) => {
  if (!Number.isInteger(v) || v < 0) p.push(`${nome} precisa ser um inteiro >= 0 (veio ${JSON.stringify(v)})`);
};
const umDe = (opcoes) => (v, nome, p) => {
  if (!opcoes.includes(v)) p.push(`${nome} precisa ser um de ${opcoes.join(', ')} (veio ${JSON.stringify(v)})`);
};

/**
 * Problemas do JSON recebido em relação ao contrato da spec telemetria-wifi
 * (lista vazia = dentro do contrato).
 */
export function problemasDoContrato(dados) {
  const p = [];
  if (!ehObjeto(dados)) return ['o corpo precisa ser um objeto JSON'];

  conferirCampos(dados, 'corpo', CAMPOS, {
    device: (v, n, pr) => {
      if (typeof v !== 'string' || !/^[A-Za-z0-9_-]{1,23}$/.test(v)) pr.push(`${n} precisa ser texto com letras, dígitos, - ou _ (veio ${JSON.stringify(v)})`);
    },
    seq: (v, n, pr) => {
      if (!Number.isInteger(v) || v < 1) pr.push(`${n} precisa ser um inteiro >= 1 (veio ${JSON.stringify(v)})`);
    },
    uptime_s: inteiro,
    leitura: (v, n, pr) => {
      if (v === null) return;
      conferirCampos(v, n, ['v', 'i', 'p', 'f', 'fp', 'energia_pzem_wh'], {
        v: numero, i: numero, p: numero, f: numero, fp: numero, energia_pzem_wh: inteiro,
      }, pr);
    },
    energia: (v, n, pr) => conferirCampos(v, n, ['dia', 'dia_wh', 'anterior_wh', 'total_wh', 'pmed_w', 'lacunas_recuperadas'], {
      dia: inteiro, dia_wh: numero, anterior_wh: numero, total_wh: numero, pmed_w: numero, lacunas_recuperadas: inteiro,
    }, pr),
    meta: (v, n, pr) => conferirCampos(v, n, ['estado', 'uso_pct', 'projecao_wh', 'alerta_potencia'], {
      estado: umDe(ESTADOS_META),
      uso_pct: inteiro,
      projecao_wh: numero,
      alerta_potencia: (b, nb, prb) => { if (typeof b !== 'boolean') prb.push(`${nb} precisa ser true ou false`); },
    }, pr),
    anomalias: (v, n, pr) => {
      if (!Array.isArray(v)) { pr.push(`${n} precisa ser uma lista`); return; }
      v.forEach((a, i) => umDe(ANOMALIAS)(a, `${n}[${i}]`, pr));
      if (new Set(v).size !== v.length) pr.push(`${n} tem nomes repetidos`);
    },
    tensao_faixa: umDe(FAIXAS),
    saltos: inteiro,
    pzem: (v, n, pr) => conferirCampos(v, n, ['ok', 'falha'], { ok: inteiro, falha: inteiro }, pr),
  }, p);
  return p;
}

/* ------------------------------------------------------------ servidor -- */
function responder(res, status, corpo) {
  const texto = typeof corpo === 'string' ? corpo : JSON.stringify(corpo);
  res.writeHead(status, { 'Content-Type': typeof corpo === 'string' ? 'text/plain; charset=utf-8' : 'application/json' });
  res.end(texto);
}

function resumoDaLeitura(d) {
  const l = d.leitura;
  const medida = l ? `V=${l.v} I=${l.i} P=${l.p} W f=${l.f} FP=${l.fp}` : 'sem leitura do PZEM';
  const anomalias = d.anomalias.length ? d.anomalias.join(',') : '-';
  return `${d.device} seq=${d.seq} t=${d.uptime_s}s | ${medida} | dia=${d.energia.dia_wh} Wh meta=${d.meta.estado} | anomalias=${anomalias}`;
}

/**
 * Cria o servidor (sem escutar). arquivo = onde gravar as leituras aceitas
 * (zerado aqui); log = função de saída (console.log por padrão).
 */
export function criarServidor({ arquivo = ARQUIVO_PADRAO, log = console.log } = {}) {
  mkdirSync(path.dirname(arquivo), { recursive: true });
  writeFileSync(arquivo, '');
  const estado = { aceitas: 0, recusadas: 0, ultima: null, ultimoSeq: new Map() };

  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');

    if (req.method === 'GET' && url.pathname === '/') {
      responder(res, 200, `EnergyTrack backend de teste\naceitas=${estado.aceitas} recusadas=${estado.recusadas}\nPOST ${ROTA}\n`);
      return;
    }
    if (req.method === 'GET' && url.pathname === `${ROTA}/ultima`) {
      if (estado.ultima) responder(res, 200, estado.ultima);
      else responder(res, 404, { erro: 'nenhuma leitura recebida ainda' });
      return;
    }
    if (url.pathname !== ROTA) { responder(res, 404, { erro: `rota desconhecida: ${req.method} ${url.pathname}` }); return; }
    if (req.method !== 'POST') { responder(res, 405, { erro: `use POST em ${ROTA}` }); return; }

    const partes = [];
    let tamanho = 0;
    let estourou = false;
    req.on('data', (parte) => {
      tamanho += parte.length;
      if (tamanho > LIMITE_CORPO) estourou = true;
      else partes.push(parte);
    });
    req.on('end', () => {
      const origem = req.socket.remoteAddress ?? '?';
      const recusar = (problemas) => {
        estado.recusadas++;
        log(`[${new Date().toLocaleTimeString()}] RECUSADO de ${origem}: ${problemas.join('; ')}`);
        responder(res, 400, { erro: 'fora do contrato', problemas });
      };
      if (estourou) { recusar([`corpo maior que ${LIMITE_CORPO} bytes`]); return; }

      let dados;
      try {
        dados = JSON.parse(Buffer.concat(partes).toString('utf-8'));
      } catch (err) {
        recusar([`o corpo não é JSON válido (${err.message})`]);
        return;
      }
      const problemas = problemasDoContrato(dados);
      if (problemas.length) { recusar(problemas); return; }

      const registro = { recebido_em: new Date().toISOString(), origem, dados };
      appendFileSync(arquivo, `${JSON.stringify(registro)}\n`);
      estado.aceitas++;
      estado.ultima = registro;

      const anterior = estado.ultimoSeq.get(dados.device);
      if (anterior !== undefined && dados.seq > anterior + 1) {
        log(`  !! faltaram ${dados.seq - anterior - 1} envio(s) de ${dados.device} (seq ${anterior + 1} a ${dados.seq - 1})`);
      } else if (anterior !== undefined && dados.seq <= anterior) {
        log(`  -- ${dados.device} recomeçou a sequência (a placa reiniciou?)`);
      }
      estado.ultimoSeq.set(dados.device, dados.seq);
      log(`[${new Date().toLocaleTimeString()}] ${resumoDaLeitura(dados)}`);
      responder(res, 201, { ok: true, seq: dados.seq });
    });
  });
  server.estado = estado;
  return server;
}

/* ---------------------------------------------------------------- main -- */
function enderecosLocais() {
  return Object.values(os.networkInterfaces())
    .flat()
    .filter((i) => i && i.family === 'IPv4' && !i.internal)
    .map((i) => i.address);
}

function opcao(nome) {
  const i = process.argv.indexOf(nome);
  return i > 0 ? process.argv[i + 1] : undefined;
}

function main() {
  const porta = Number(opcao('--porta') ?? 8080);
  const arquivo = path.resolve(opcao('--arquivo') ?? ARQUIVO_PADRAO);
  if (!Number.isInteger(porta) || porta <= 0 || porta > 65535) {
    console.error('porta inválida — uso: node tools/backend-teste.mjs [--porta 8080] [--arquivo saida.jsonl]');
    process.exit(2);
  }
  const server = criarServidor({ arquivo });
  server.listen(porta, '0.0.0.0', () => {
    console.log(`backend de teste ouvindo na porta ${porta}; gravando em ${path.relative(process.cwd(), arquivo)}`);
    const ips = enderecosLocais();
    if (ips.length === 0) console.log('nenhum endereço de rede encontrado: o PC está conectado?');
    for (const ip of ips) console.log(`  URL para o menuconfig (CONFIG_ET_BACKEND_URL): http://${ip}:${porta}${ROTA}`);
    console.log('Ctrl+C para parar.\n');
  });
}

const chamadoDireto = process.argv[1] && path.resolve(process.argv[1]).toLowerCase() === fileURLToPath(import.meta.url).toLowerCase();
if (chamadoDireto) main();
