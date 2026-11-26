"""US-010: a placa entrega as leituras ao backend de verdade (POST /api/leituras)."""

import copy
import json
import re
import sqlite3
from datetime import UTC, datetime
from pathlib import Path

import pytest
from alembic.autogenerate import compare_metadata
from alembic.runtime.migration import MigrationContext
from alembic.script import ScriptDirectory
from fastapi.testclient import TestClient
from sqlalchemy import create_engine

from energytrack_backend.banco import Base, config_alembic
from energytrack_backend.config import ROTA_LEITURAS, url_do_banco
from energytrack_backend.principal import criar_app

PACOTE = Path(__file__).resolve().parent.parent / "energytrack_backend"


def linhas_do_banco(banco: Path) -> list[dict]:
    with sqlite3.connect(banco) as con:
        con.row_factory = sqlite3.Row
        return [dict(r) for r in con.execute("select * from leituras order by id")]


def postar(cliente: TestClient, corpo) -> "httpx.Response":  # noqa: F821
    texto = corpo if isinstance(corpo, str) else json.dumps(corpo)
    return cliente.post(ROTA_LEITURAS, content=texto, headers={"Content-Type": "application/json"})


@pytest.mark.ac("AC-029")
def test_json_da_placa_e_aceito_e_guardado_com_a_hora_de_chegada(cliente, banco, jsons_da_placa):
    antes = datetime.now(UTC).replace(tzinfo=None)
    for nome, corpo in jsons_da_placa.items():
        r = postar(cliente, corpo)
        assert r.status_code == 201, f"JSON da placa ({nome}) recusado: {r.text}"
        assert r.json() == {"ok": True, "seq": corpo["seq"]}
    depois = datetime.now(UTC).replace(tzinfo=None)

    linhas = linhas_do_banco(banco)
    assert len(linhas) == len(jsons_da_placa)
    for linha, corpo in zip(linhas, jsons_da_placa.values(), strict=True):
        assert json.loads(linha["dados"]) == corpo, "o JSON guardado é diferente do recebido"
        assert (linha["device"], linha["seq"], linha["uptime_s"]) == (corpo["device"], corpo["seq"], corpo["uptime_s"])
        recebido = datetime.fromisoformat(linha["recebido_em"])
        assert antes <= recebido <= depois, f"hora de chegada fora do intervalo do envio: {recebido}"


@pytest.mark.ac("AC-029")
def test_os_casos_do_codigo_em_c_estao_todos_la(jsons_da_placa):
    # Com a ponte, chegam os 4 JSON do teste em C; sozinho, o exemplo do contrato.
    assert {"leitura", "sem-leitura"} <= set(jsons_da_placa)
    assert jsons_da_placa["sem-leitura"]["leitura"] is None


def _mudar(base: dict, caminho: str, valor) -> dict:
    d = copy.deepcopy(base)
    alvo, *resto = caminho.split(".")
    if resto:
        d[alvo][resto[0]] = valor
    elif valor is _APAGAR:
        del d[alvo]
    else:
        d[alvo] = valor
    return d


_APAGAR = object()

CASOS_FORA_DO_CONTRATO = [
    # (nome, mudança no JSON válido ou corpo em texto, trecho esperado num problema)
    ("não é JSON", '{"device": "et-1", ', "não é JSON"),
    ("lista em vez de objeto", "[]", "o corpo precisa ser um objeto"),
    ("sem o campo meta", ("meta", _APAGAR), "corpo.meta está faltando"),
    ("campo a mais", ("temperatura", 25), "corpo.temperatura não faz parte"),
    ("tensão como texto", ("leitura.v", "221.3"), "corpo.leitura.v"),
    ("tensão NaN", ("leitura.v", float("nan")), "corpo.leitura.v"),
    ("corrente negativa", ("leitura.i", -1.0), "corpo.leitura.i"),
    ("anomalia desconhecida", ("anomalias", ["FALTA_DE_LUZ"]), "corpo.anomalias[0]"),
    ("anomalia repetida", ("anomalias", ["FP_BAIXO", "FP_BAIXO"]), "corpo.anomalias tem nomes repetidos"),
    ("estado de meta desconhecido", ("meta.estado", "BOM"), "corpo.meta.estado precisa ser um de 'OK', 'PROJECAO_ACIMA', 'ATENCAO' ou 'ESTOURADA'"),
    ("alerta como número", ("meta.alerta_potencia", 1), "corpo.meta.alerta_potencia"),
    ("seq 0", ("seq", 0), "corpo.seq"),
    ("energia do PZEM fracionária", ("leitura.energia_pzem_wh", 1.5), "corpo.leitura.energia_pzem_wh"),
    ("device com espaço", ("device", "et 1"), "corpo.device"),
    ("faixa de tensão desconhecida", ("tensao_faixa", "boa"), "corpo.tensao_faixa"),
]


@pytest.mark.ac("AC-030")
@pytest.mark.parametrize(("nome", "mudanca", "esperado"), CASOS_FORA_DO_CONTRATO, ids=[c[0] for c in CASOS_FORA_DO_CONTRATO])
def test_json_fora_do_contrato_e_recusado_e_nao_e_guardado(cliente, banco, leitura_exemplo, nome, mudanca, esperado):
    corpo = mudanca if isinstance(mudanca, str) else _mudar(leitura_exemplo, *mudanca)
    texto = corpo if isinstance(corpo, str) else json.dumps(corpo)  # json.dumps escreve NaN, como um firmware com bug
    r = postar(cliente, texto)
    assert r.status_code == 400, f"{nome}: deveria ser recusado, veio {r.status_code} {r.text}"
    resposta = r.json()
    assert resposta["erro"] == "fora do contrato"
    assert any(esperado in p for p in resposta["problemas"]), f"{nome}: nenhum problema cita '{esperado}': {resposta['problemas']}"
    assert linhas_do_banco(banco) == [], f"{nome}: um corpo recusado foi guardado"


@pytest.mark.ac("AC-030")
def test_corpo_maior_que_16_kib_e_recusado(cliente, banco, leitura_exemplo):
    # JSON válido, mas com espaços até passar de 16 KiB: só o tamanho é problema.
    texto = json.dumps(leitura_exemplo) + " " * (16 * 1024)
    r = postar(cliente, texto)
    assert r.status_code == 400
    assert any("16384 bytes" in p for p in r.json()["problemas"])
    assert linhas_do_banco(banco) == []
    # E o mesmo JSON sem o enchimento passa.
    assert postar(cliente, leitura_exemplo).status_code == 201


@pytest.mark.ac("AC-030")
def test_varios_problemas_saem_todos_de_uma_vez(cliente, leitura_exemplo):
    corpo = _mudar(_mudar(leitura_exemplo, "seq", 0), "meta.estado", "BOM")
    problemas = postar(cliente, corpo).json()["problemas"]
    assert any(p.startswith("corpo.seq") for p in problemas)
    assert any(p.startswith("corpo.meta.estado") for p in problemas)


@pytest.mark.ac("AC-031")
def test_leituras_sobrevivem_a_reiniciar_o_backend(banco, jsons_da_placa):
    url = url_do_banco(banco)
    assert not banco.exists()

    with TestClient(criar_app(url)) as primeira:
        for corpo in jsons_da_placa.values():
            assert postar(primeira, corpo).status_code == 201
        assert primeira.get("/").json()["leituras"] == len(jsons_da_placa)
    primeira.app.state.engine.dispose()  # "para" o backend: solta o arquivo

    with TestClient(criar_app(url)) as segunda:
        assert segunda.get("/").json()["leituras"] == len(jsons_da_placa)
        assert postar(segunda, jsons_da_placa["leitura"]).status_code == 201
        assert segunda.get("/").json()["leituras"] == len(jsons_da_placa) + 1
    segunda.app.state.engine.dispose()
    assert [json.loads(l["dados"]) for l in linhas_do_banco(banco)][: len(jsons_da_placa)] == list(jsons_da_placa.values())


@pytest.mark.ac("AC-031")
def test_o_banco_nasce_das_migracoes_do_alembic(banco):
    url = url_do_banco(banco)
    criar_app(url).state.engine.dispose()

    head = ScriptDirectory.from_config(config_alembic(url)).get_current_head()
    with sqlite3.connect(banco) as con:
        assert con.execute("select version_num from alembic_version").fetchone()[0] == head

    # A migração cria exatamente a tabela do modelo (nada a migrar).
    engine = create_engine(url)
    with engine.connect() as con:
        diferencas = compare_metadata(MigrationContext.configure(con), Base.metadata)
    engine.dispose()
    assert diferencas == [], f"o modelo e as migrações divergem: {diferencas}"

    # E ninguém cria tabela por fora das migrações.
    for arquivo in PACOTE.rglob("*.py"):
        assert not re.search(r"create_all\s*\(", arquivo.read_text(encoding="utf-8")), f"{arquivo.name} chama create_all"
