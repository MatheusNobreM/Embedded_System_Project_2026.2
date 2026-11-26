"""Injeção de dependências: monta sessão → repositório → serviço a cada requisição.

É o único lugar da camada api que conhece o banco. Nos testes, qualquer peça
pode ser trocada com app.dependency_overrides (ex.: obter_relogio).
"""

from collections.abc import Iterator
from typing import Annotated

from fastapi import Depends, Request
from sqlalchemy.orm import Session

from ..core.erros import ForaDoContrato
from ..core.relogio import Relogio, RelogioDoSistema
from ..repositories.base import RepositorioDeLeituras
from ..repositories.leituras_sql import RepositorioSQLDeLeituras
from ..services.consultas import ServicoDeConsultas
from ..services.telemetria import ServicoDeTelemetria

LIMITE_CORPO = 16 * 1024  # bytes, como no backend de teste
_RELOGIO = RelogioDoSistema()


def obter_sessao(request: Request) -> Iterator[Session]:
    """Uma sessão do banco por requisição."""
    with request.app.state.sessoes() as s:
        yield s


def obter_relogio() -> Relogio:
    return _RELOGIO


def obter_repositorio(sessao: Annotated[Session, Depends(obter_sessao)]) -> RepositorioDeLeituras:
    return RepositorioSQLDeLeituras(sessao)


def obter_servico_de_telemetria(
    repositorio: Annotated[RepositorioDeLeituras, Depends(obter_repositorio)],
    relogio: Annotated[Relogio, Depends(obter_relogio)],
) -> ServicoDeTelemetria:
    return ServicoDeTelemetria(repositorio, relogio)


def obter_servico_de_consultas(
    repositorio: Annotated[RepositorioDeLeituras, Depends(obter_repositorio)],
) -> ServicoDeConsultas:
    return ServicoDeConsultas(repositorio)


async def limitar_corpo(request: Request) -> None:
    if len(await request.body()) > LIMITE_CORPO:
        raise ForaDoContrato([f"corpo maior que {LIMITE_CORPO} bytes"])


Telemetria = Annotated[ServicoDeTelemetria, Depends(obter_servico_de_telemetria)]
Consultas = Annotated[ServicoDeConsultas, Depends(obter_servico_de_consultas)]
