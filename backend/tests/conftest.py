"""Base dos testes do backend.

- `banco`/`cliente`: um SQLite novo por teste e a aplicação servida pelo TestClient.
- `jsons_da_placa`: os JSON que o código em C da placa monta. A ponte
  (tests/backend-api.spec.test.mjs) gera esses JSON pelo teste em C da
  telemetria e passa o arquivo pela variável ET_JSON_PLACA. Rodando o pytest
  sozinho, vale o exemplo do contrato (README do firmware, seção 6).
- Marcador `ac("AC-xxx")`: o critério de aceite que o teste prova. Com
  ET_RESULTADOS definida, o resultado de cada teste (com os critérios) é
  gravado nesse JSON, que a ponte transforma num teste por critério para o
  onp-spec.
"""

import copy
import json
import os
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from energytrack_backend.config import url_do_banco
from energytrack_backend.principal import criar_app

EXEMPLO_DO_CONTRATO = {
    "device": "et-a4cf12345678",
    "seq": 121,
    "uptime_s": 602,
    "leitura": {"v": 221.3, "i": 1.234, "p": 271.9, "f": 60.0, "fp": 0.98, "energia_pzem_wh": 102},
    "energia": {
        "dia": 1,
        "dia_wh": 45.312,
        "anterior_wh": 0.000,
        "total_wh": 45.312,
        "pmed_w": 271.5,
        "lacunas_recuperadas": 0,
    },
    "meta": {"estado": "OK", "uso_pct": 0, "projecao_wh": 6516.000, "alerta_potencia": False},
    "anomalias": [],
    "tensao_faixa": "adequada",
    "saltos": 0,
    "pzem": {"ok": 601, "falha": 0},
}


@pytest.fixture
def banco(tmp_path: Path) -> Path:
    return tmp_path / "teste.db"


@pytest.fixture
def cliente(banco: Path):
    with TestClient(criar_app(url_do_banco(banco))) as c:
        yield c


@pytest.fixture
def jsons_da_placa() -> dict[str, dict]:
    arquivo = os.environ.get("ET_JSON_PLACA")
    if arquivo:
        return json.loads(Path(arquivo).read_text(encoding="utf-8"))
    sem_leitura = {**copy.deepcopy(EXEMPLO_DO_CONTRATO), "leitura": None}
    return {"leitura": copy.deepcopy(EXEMPLO_DO_CONTRATO), "sem-leitura": sem_leitura}


@pytest.fixture
def leitura_exemplo(jsons_da_placa) -> dict:
    """Um JSON válido da placa, para variar campo a campo nos testes."""
    return copy.deepcopy(jsons_da_placa["leitura"])


# ------------------------------------------------- resultado para a ponte --
_criterios: dict[str, list[str]] = {}
_resultados: dict[str, dict] = {}


def pytest_collection_modifyitems(items):
    for item in items:
        _criterios[item.nodeid] = [m.args[0] for m in item.iter_markers("ac")]


def pytest_runtest_logreport(report):
    r = _resultados.setdefault(
        report.nodeid,
        {"nodeid": report.nodeid, "criterios": _criterios.get(report.nodeid, []), "resultado": "passou"},
    )
    if report.failed:
        r["resultado"] = "falhou"
        r["erro"] = report.longreprtext[-3000:]
    elif report.skipped and r["resultado"] != "falhou":
        r["resultado"] = "pulado"


def pytest_sessionfinish(session):
    destino = os.environ.get("ET_RESULTADOS")
    if destino:
        Path(destino).write_text(json.dumps(list(_resultados.values()), ensure_ascii=False), encoding="utf-8")
