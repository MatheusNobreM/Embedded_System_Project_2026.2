"""Consultas do app (US-011): placas, leitura atual e histórico de um período."""

from datetime import datetime
from typing import Annotated

from fastapi import APIRouter, Query

from ...schemas.leituras import Dispositivo, Erro, LeituraGuardada
from ...services.consultas import LIMITE_MAXIMO, LIMITE_PADRAO
from ..deps import Consultas

router = APIRouter(prefix="/api/dispositivos", tags=["app"])


@router.get("", summary="Placas que já mandaram dados")
def listar_dispositivos(consultas: Consultas) -> list[Dispositivo]:
    return consultas.listar_dispositivos()


@router.get(
    "/{device}/ultima",
    summary="Leitura mais recente de uma placa",
    responses={404: {"model": Erro, "description": "A placa nunca mandou leituras"}},
)
def ultima_leitura(device: str, consultas: Consultas) -> LeituraGuardada:
    return consultas.ultima(device)


@router.get("/{device}/leituras", summary="Histórico de uma placa, em ordem cronológica")
def historico(
    device: str,
    consultas: Consultas,
    desde: Annotated[datetime | None, Query(description="início do período (ISO 8601; sem fuso = UTC)")] = None,
    ate: Annotated[datetime | None, Query(description="fim do período (ISO 8601; sem fuso = UTC)")] = None,
    limite: Annotated[
        int, Query(ge=1, le=LIMITE_MAXIMO, description="máximo de leituras; ficam as mais recentes do período")
    ] = LIMITE_PADRAO,
) -> list[LeituraGuardada]:
    return consultas.historico(device, desde, ate, limite)
