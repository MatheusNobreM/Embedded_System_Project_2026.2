"""US-011: o app consulta as placas, a leitura atual e o histórico."""

import copy
import json
from datetime import datetime, timedelta, timezone

import pytest

from energytrack_backend import rotas_leituras
from energytrack_backend.banco import Leitura
from energytrack_backend.config import ROTA_LEITURAS

T0 = datetime(2026, 11, 25, 14, 0, 0)  # UTC, sem fuso (como no banco)


@pytest.fixture
def relogio(monkeypatch):
    """Controla a hora de chegada que o POST carimba."""
    agora = {"t": T0}
    monkeypatch.setattr(rotas_leituras, "agora_utc", lambda: agora["t"])
    return agora


def enviar(cliente, relogio, corpo: dict, quando: datetime) -> None:
    relogio["t"] = quando
    r = cliente.post(ROTA_LEITURAS, content=json.dumps(corpo), headers={"Content-Type": "application/json"})
    assert r.status_code == 201, r.text


def da_placa(base: dict, device: str, seq: int) -> dict:
    d = copy.deepcopy(base)
    d["device"], d["seq"], d["uptime_s"] = device, seq, seq * 5
    return d


def iso(t: datetime) -> str:
    return t.isoformat(timespec="milliseconds") + "Z"


@pytest.mark.ac("AC-032")
def test_o_app_ve_quais_placas_mandaram_dados(cliente, relogio, leitura_exemplo):
    assert cliente.get("/api/dispositivos").json() == []

    for k in range(3):
        enviar(cliente, relogio, da_placa(leitura_exemplo, "et-aaaaaaaaaaaa", k + 1), T0 + timedelta(seconds=5 * k))
    enviar(cliente, relogio, da_placa(leitura_exemplo, "et-bbbbbbbbbbbb", 1), T0 + timedelta(seconds=2))

    r = cliente.get("/api/dispositivos")
    assert r.status_code == 200
    assert r.json() == [
        {"device": "et-aaaaaaaaaaaa", "leituras": 3, "primeira_em": iso(T0), "ultima_em": iso(T0 + timedelta(seconds=10))},
        {"device": "et-bbbbbbbbbbbb", "leituras": 1, "primeira_em": iso(T0 + timedelta(seconds=2)), "ultima_em": iso(T0 + timedelta(seconds=2))},
    ]


@pytest.mark.ac("AC-033")
def test_o_app_ve_a_leitura_mais_recente_de_uma_placa(cliente, relogio, jsons_da_placa):
    corpos = [da_placa(c, "et-aaaaaaaaaaaa", k + 1) for k, c in enumerate(jsons_da_placa.values())]
    for k, corpo in enumerate(corpos):
        enviar(cliente, relogio, corpo, T0 + timedelta(seconds=5 * k))
    # Outra placa depois: não pode virar a "última" da primeira.
    enviar(cliente, relogio, da_placa(corpos[0], "et-bbbbbbbbbbbb", 1), T0 + timedelta(minutes=1))

    r = cliente.get("/api/dispositivos/et-aaaaaaaaaaaa/ultima")
    assert r.status_code == 200
    assert r.json() == {"recebido_em": iso(T0 + timedelta(seconds=5 * (len(corpos) - 1))), "dados": corpos[-1]}


@pytest.mark.ac("AC-033")
def test_placa_que_nunca_mandou_dados_da_404(cliente):
    r = cliente.get("/api/dispositivos/et-000000000000/ultima")
    assert r.status_code == 404
    assert "et-000000000000" in r.json()["erro"]


@pytest.fixture
def dez_leituras(cliente, relogio, leitura_exemplo):
    """10 leituras da mesma placa, uma a cada 5 s a partir de T0 (seq 1..10)."""
    for k in range(10):
        enviar(cliente, relogio, da_placa(leitura_exemplo, "et-aaaaaaaaaaaa", k + 1), T0 + timedelta(seconds=5 * k))


def seqs(resposta) -> list[int]:
    assert resposta.status_code == 200, resposta.text
    return [l["dados"]["seq"] for l in resposta.json()]


URL = "/api/dispositivos/et-aaaaaaaaaaaa/leituras"


@pytest.mark.ac("AC-034")
def test_historico_do_periodo_em_ordem_cronologica(cliente, dez_leituras):
    # Tudo, da mais antiga para a mais nova, com a hora de chegada.
    r = cliente.get(URL)
    assert seqs(r) == list(range(1, 11))
    assert r.json()[0]["recebido_em"] == iso(T0)

    # Período fechado nas duas pontas: de T0+10s a T0+25s = seq 3..6.
    r = cliente.get(URL, params={"desde": iso(T0 + timedelta(seconds=10)), "ate": iso(T0 + timedelta(seconds=25))})
    assert seqs(r) == [3, 4, 5, 6]

    # Só uma das pontas.
    assert seqs(cliente.get(URL, params={"desde": iso(T0 + timedelta(seconds=40))})) == [9, 10]
    assert seqs(cliente.get(URL, params={"ate": iso(T0 + timedelta(seconds=5))})) == [1, 2]

    # Data com fuso: 11:00:20-03:00 = 14:00:20 UTC.
    fuso = timezone(timedelta(hours=-3))
    desde = (T0 + timedelta(seconds=20)).replace(tzinfo=timezone.utc).astimezone(fuso).isoformat()
    assert seqs(cliente.get(URL, params={"desde": desde})) == [5, 6, 7, 8, 9, 10]


@pytest.mark.ac("AC-034")
def test_limite_fica_com_as_mais_recentes_do_periodo(cliente, dez_leituras):
    assert seqs(cliente.get(URL, params={"limite": 3})) == [8, 9, 10]
    r = cliente.get(URL, params={"ate": iso(T0 + timedelta(seconds=25)), "limite": 2})
    assert seqs(r) == [5, 6]


@pytest.mark.ac("AC-034")
def test_sem_limite_valem_720(cliente, leitura_exemplo):
    # 725 leituras direto no banco (o POST já tem os testes dele).
    with cliente.app.state.sessoes() as s:
        for k in range(725):
            d = da_placa(leitura_exemplo, "et-aaaaaaaaaaaa", k + 1)
            s.add(Leitura(device=d["device"], seq=d["seq"], uptime_s=d["uptime_s"], recebido_em=T0 + timedelta(seconds=5 * k), dados=d))
        s.commit()
    r = seqs(cliente.get(URL))
    assert len(r) == 720
    assert r == list(range(6, 726))


@pytest.mark.ac("AC-034")
def test_periodo_vazio_e_parametro_invalido(cliente, dez_leituras):
    assert seqs(cliente.get(URL, params={"desde": iso(T0 + timedelta(hours=1))})) == []
    assert seqs(cliente.get("/api/dispositivos/et-000000000000/leituras")) == []

    for params in ({"desde": "ontem"}, {"ate": "25/11/2026"}, {"limite": 0}, {"limite": 5001}, {"limite": "muitas"}):
        r = cliente.get(URL, params=params)
        assert r.status_code == 422, f"{params}: deveria ser recusado, veio {r.status_code}"
