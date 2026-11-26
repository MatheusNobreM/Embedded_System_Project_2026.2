"""Consultas do app (US-011): placas, leitura atual e histórico de um período."""

from datetime import datetime
from typing import Annotated, Any

from fastapi import APIRouter, Depends, FastAPI, Query
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from sqlalchemy import func, select
from sqlalchemy.orm import Session

from .banco import Leitura, hora_iso, para_utc_sem_fuso, sessao

LIMITE_PADRAO = 720  # 1 h de leituras a cada 5 s
LIMITE_MAXIMO = 5000

router = APIRouter(prefix="/api/dispositivos", tags=["app"])


class Dispositivo(BaseModel):
    device: str = Field(description='identificador da placa ("et-" + MAC)')
    leituras: int = Field(description="quantas leituras ela já mandou")
    primeira_em: str = Field(description="hora da primeira leitura (UTC, ISO 8601)")
    ultima_em: str = Field(description="hora da última leitura (UTC, ISO 8601)")


class LeituraGuardada(BaseModel):
    recebido_em: str = Field(description="hora de chegada no backend (UTC, ISO 8601)", examples=["2026-11-25T14:03:05.123Z"])
    dados: dict[str, Any] = Field(description="o JSON da placa como foi recebido (ver POST /api/leituras)")


class Erro(BaseModel):
    erro: str


def _guardada(l: Leitura) -> LeituraGuardada:
    return LeituraGuardada(recebido_em=hora_iso(l.recebido_em), dados=l.dados)


@router.get("", summary="Placas que já mandaram dados")
def listar_dispositivos(s: Session = Depends(sessao)) -> list[Dispositivo]:
    linhas = s.execute(
        select(Leitura.device, func.count(), func.min(Leitura.recebido_em), func.max(Leitura.recebido_em))
        .group_by(Leitura.device)
        .order_by(Leitura.device)
    ).all()
    return [
        Dispositivo(device=d, leituras=n, primeira_em=hora_iso(primeira), ultima_em=hora_iso(ultima))
        for d, n, primeira, ultima in linhas
    ]


@router.get(
    "/{device}/ultima",
    summary="Leitura mais recente de uma placa",
    responses={404: {"model": Erro, "description": "A placa nunca mandou leituras"}},
)
def ultima_leitura(device: str, s: Session = Depends(sessao)) -> LeituraGuardada:
    l = s.scalar(
        select(Leitura).where(Leitura.device == device).order_by(Leitura.recebido_em.desc(), Leitura.id.desc()).limit(1)
    )
    if l is None:
        return JSONResponse(status_code=404, content={"erro": f"a placa {device} nunca mandou leituras"})
    return _guardada(l)


@router.get("/{device}/leituras", summary="Histórico de uma placa, em ordem cronológica")
def historico(
    device: str,
    desde: Annotated[datetime | None, Query(description="início do período (ISO 8601; sem fuso = UTC)")] = None,
    ate: Annotated[datetime | None, Query(description="fim do período (ISO 8601; sem fuso = UTC)")] = None,
    limite: Annotated[
        int, Query(ge=1, le=LIMITE_MAXIMO, description="máximo de leituras; ficam as mais recentes do período")
    ] = LIMITE_PADRAO,
    s: Session = Depends(sessao),
) -> list[LeituraGuardada]:
    consulta = select(Leitura).where(Leitura.device == device)
    if desde is not None:
        consulta = consulta.where(Leitura.recebido_em >= para_utc_sem_fuso(desde))
    if ate is not None:
        consulta = consulta.where(Leitura.recebido_em <= para_utc_sem_fuso(ate))
    # As mais recentes do período, devolvidas da mais antiga para a mais nova.
    recentes = s.scalars(consulta.order_by(Leitura.recebido_em.desc(), Leitura.id.desc()).limit(limite)).all()
    return [_guardada(l) for l in reversed(recentes)]


def registrar(app: FastAPI) -> None:
    app.include_router(router)
