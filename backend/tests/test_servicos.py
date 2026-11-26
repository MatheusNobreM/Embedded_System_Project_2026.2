"""US-013: as regras (serviços) funcionam sem banco e sem HTTP.

O repositório aqui é uma lista em memória que segue o Protocol
RepositorioDeLeituras, e o relógio é fixo. Nada de FastAPI, SQLite ou
TestClient: se o serviço depender de algo disso, estes testes quebram.
"""

import copy
import logging
import subprocess
import sys
from dataclasses import replace
from datetime import datetime, timedelta, timezone

import pytest

from energytrack_backend.core.erros import LimiteInvalido, NaoEncontrado
from energytrack_backend.repositories.base import (
    LeituraRegistrada,
    NovaLeitura,
    RepositorioDeLeituras,
    ResumoDoDispositivo,
)
from energytrack_backend.schemas.telemetria import LeituraDaPlaca
from energytrack_backend.services.consultas import ServicoDeConsultas
from energytrack_backend.services.telemetria import ServicoDeTelemetria

T0 = datetime(2026, 11, 25, 14, 0, 0)


class RepositorioEmMemoria:
    """Implementa RepositorioDeLeituras numa lista; anota as chamadas de histórico."""

    def __init__(self) -> None:
        self.leituras: list[LeituraRegistrada] = []
        self.pedidos_de_historico: list[tuple] = []

    def adicionar(self, nova: NovaLeitura) -> LeituraRegistrada:
        registro = LeituraRegistrada(id=len(self.leituras) + 1, **vars(nova))
        self.leituras.append(registro)
        return registro

    def ultimo_seq(self, device: str) -> int | None:
        da_placa = [l for l in self.leituras if l.device == device]
        return da_placa[-1].seq if da_placa else None

    def ultima(self, device: str) -> LeituraRegistrada | None:
        da_placa = sorted((l for l in self.leituras if l.device == device), key=lambda l: (l.recebido_em, l.id))
        return da_placa[-1] if da_placa else None

    def historico(self, device, desde, ate, limite) -> list[LeituraRegistrada]:
        self.pedidos_de_historico.append((device, desde, ate, limite))
        do_periodo = sorted(
            (
                l
                for l in self.leituras
                if l.device == device and (desde is None or l.recebido_em >= desde) and (ate is None or l.recebido_em <= ate)
            ),
            key=lambda l: (l.recebido_em, l.id),
        )
        return do_periodo[-limite:]

    def resumo_por_dispositivo(self) -> list[ResumoDoDispositivo]:
        devices = sorted({l.device for l in self.leituras})
        resumo = []
        for d in devices:
            horas = [l.recebido_em for l in self.leituras if l.device == d]
            resumo.append(ResumoDoDispositivo(device=d, leituras=len(horas), primeira_em=min(horas), ultima_em=max(horas)))
        return resumo

    def contar(self) -> int:
        return len(self.leituras)


class RelogioFixo:
    def __init__(self, t: datetime):
        self.t = t

    def agora(self) -> datetime:
        return self.t


def test_o_repositorio_em_memoria_segue_o_protocolo():
    # Se o Protocol ganhar um método, este teste lembra de atualizar o falso.
    repo: RepositorioDeLeituras = RepositorioEmMemoria()
    metodos = {n for n in vars(RepositorioDeLeituras) if not n.startswith("_")}
    assert metodos <= set(dir(repo))


def da_placa(base: dict, seq: int, device: str = "et-aaaaaaaaaaaa") -> LeituraDaPlaca:
    d = copy.deepcopy(base)
    d["device"], d["seq"], d["uptime_s"] = device, seq, seq * 5
    return LeituraDaPlaca.model_validate(d)


@pytest.mark.ac("AC-036")
def test_recebimento_guarda_com_a_hora_do_relogio_e_nota_envios_perdidos(leitura_exemplo, caplog):
    repo, relogio = RepositorioEmMemoria(), RelogioFixo(T0)
    servico = ServicoDeTelemetria(repo, relogio)

    resultados = []
    with caplog.at_level(logging.INFO, logger="energytrack_backend"):
        for k, seq in enumerate([10, 11, 14, 1]):
            relogio.t = T0 + timedelta(seconds=5 * k)
            resultados.append(servico.receber(da_placa(leitura_exemplo, seq)))

    # Cada leitura foi entregue ao repositório com a hora do relógio e o JSON da placa.
    assert [l.seq for l in repo.leituras] == [10, 11, 14, 1]
    assert [l.recebido_em for l in repo.leituras] == [T0 + timedelta(seconds=5 * k) for k in range(4)]
    assert repo.leituras[2].dados == da_placa(leitura_exemplo, 14).model_dump(mode="json")
    assert (repo.leituras[2].device, repo.leituras[2].uptime_s) == ("et-aaaaaaaaaaaa", 70)

    # seq 14 depois do 11: 2 envios perdidos (12 e 13); seq 1 depois do 14: a placa reiniciou.
    assert [(r.seq, r.envios_perdidos, r.reiniciou) for r in resultados] == [
        (10, 0, False),
        (11, 0, False),
        (14, 2, False),
        (1, 0, True),
    ]
    assert "faltaram 2 envio(s) de et-aaaaaaaaaaaa (seq 12 a 13)" in caplog.text
    assert "et-aaaaaaaaaaaa recomeçou a sequência" in caplog.text


@pytest.mark.ac("AC-036")
def test_sequencia_e_por_placa(leitura_exemplo):
    repo = RepositorioEmMemoria()
    servico = ServicoDeTelemetria(repo, RelogioFixo(T0))
    servico.receber(da_placa(leitura_exemplo, 10, "et-aaaaaaaaaaaa"))
    # Outra placa com seq 50 não é "buraco" da primeira, e vice-versa.
    assert servico.receber(da_placa(leitura_exemplo, 50, "et-bbbbbbbbbbbb")).envios_perdidos == 0
    assert servico.receber(da_placa(leitura_exemplo, 11, "et-aaaaaaaaaaaa")).envios_perdidos == 0


def repositorio_com_dez(leitura_exemplo) -> RepositorioEmMemoria:
    repo = RepositorioEmMemoria()
    for k in range(10):
        d = da_placa(leitura_exemplo, k + 1)
        repo.adicionar(
            NovaLeitura(device=d.device, seq=d.seq, uptime_s=d.uptime_s, recebido_em=T0 + timedelta(seconds=5 * k), dados=d.model_dump(mode="json"))
        )
    return repo


@pytest.mark.ac("AC-037")
def test_placa_sem_dados_vira_nao_encontrado(leitura_exemplo):
    servico = ServicoDeConsultas(repositorio_com_dez(leitura_exemplo))
    with pytest.raises(NaoEncontrado, match="et-000000000000"):
        servico.ultima("et-000000000000")
    ultima = servico.ultima("et-aaaaaaaaaaaa")
    assert ultima.dados["seq"] == 10
    assert ultima.recebido_em == "2026-11-25T14:00:45.000Z"


@pytest.mark.ac("AC-037")
def test_historico_cronologico_com_horas_utc_e_limite(leitura_exemplo):
    repo = repositorio_com_dez(leitura_exemplo)
    servico = ServicoDeConsultas(repo)

    historico = servico.historico("et-aaaaaaaaaaaa", limite=3)
    assert [l.dados["seq"] for l in historico] == [8, 9, 10]
    assert [l.recebido_em for l in historico] == [
        "2026-11-25T14:00:35.000Z",
        "2026-11-25T14:00:40.000Z",
        "2026-11-25T14:00:45.000Z",
    ]

    # Data com fuso chega ao repositório já em UTC sem fuso.
    fuso = timezone(timedelta(hours=-3))
    desde = datetime(2026, 11, 25, 11, 0, 20, tzinfo=fuso)  # = 14:00:20 UTC
    assert [l.dados["seq"] for l in servico.historico("et-aaaaaaaaaaaa", desde=desde)] == [5, 6, 7, 8, 9, 10]
    assert repo.pedidos_de_historico[-1] == ("et-aaaaaaaaaaaa", T0 + timedelta(seconds=20), None, 720)

    # Listagem das placas também sai com horas em UTC e "Z".
    [placa] = servico.listar_dispositivos()
    assert (placa.leituras, placa.primeira_em, placa.ultima_em) == (10, "2026-11-25T14:00:00.000Z", "2026-11-25T14:00:45.000Z")
    assert servico.total_de_leituras() == 10


@pytest.mark.ac("AC-037")
@pytest.mark.parametrize("limite", [0, -1, 5001])
def test_limite_invalido_vira_erro_sem_chegar_ao_repositorio(leitura_exemplo, limite):
    repo = repositorio_com_dez(leitura_exemplo)
    with pytest.raises(LimiteInvalido):
        ServicoDeConsultas(repo).historico("et-aaaaaaaaaaaa", limite=limite)
    assert repo.pedidos_de_historico == []


@pytest.mark.ac("AC-036")
@pytest.mark.ac("AC-037")
def test_os_servicos_nao_carregam_fastapi_nem_sqlalchemy():
    # Processo novo: só os serviços (e o que eles importam) entram em sys.modules.
    codigo = (
        "import sys\n"
        "import energytrack_backend.services.telemetria, energytrack_backend.services.consultas\n"
        "print(sorted(m for m in ('fastapi', 'starlette', 'sqlalchemy', 'alembic') if m in sys.modules))\n"
    )
    r = subprocess.run([sys.executable, "-c", codigo], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    assert r.stdout.strip() == "[]", f"os serviços puxaram: {r.stdout.strip()}"
