"""POST /api/leituras: a placa entrega cada leitura (US-010)."""

import logging

from fastapi import APIRouter, Depends, FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from sqlalchemy import select
from sqlalchemy.orm import Session

from .banco import Leitura, agora_utc, sessao
from .config import ROTA_LEITURAS
from .contrato import LeituraDaPlaca, problema_em_portugues

LIMITE_CORPO = 16 * 1024  # bytes, como no backend de teste
log = logging.getLogger("energytrack_backend")

router = APIRouter(tags=["placa"])


class ForaDoContrato(Exception):
    def __init__(self, problemas: list[str]):
        self.problemas = problemas


def _recusa(request: Request, problemas: list[str]) -> JSONResponse:
    origem = request.client.host if request.client else "?"
    log.warning("RECUSADO de %s: %s", origem, "; ".join(problemas))
    return JSONResponse(status_code=400, content={"erro": "fora do contrato", "problemas": problemas})


async def _limitar_corpo(request: Request) -> None:
    if len(await request.body()) > LIMITE_CORPO:
        raise ForaDoContrato([f"corpo maior que {LIMITE_CORPO} bytes"])


@router.post(
    ROTA_LEITURAS,
    status_code=201,
    summary="A placa entrega uma leitura",
    response_description="Leitura guardada",
    responses={400: {"description": "Fora do contrato: {erro, problemas[]}"}},
    dependencies=[Depends(_limitar_corpo)],
)
def receber_leitura(corpo: LeituraDaPlaca, request: Request, s: Session = Depends(sessao)) -> dict:
    """Guarda o JSON da placa com a hora de chegada (UTC). 201 = guardado."""
    anterior = s.scalar(
        select(Leitura.seq).where(Leitura.device == corpo.device).order_by(Leitura.id.desc()).limit(1)
    )
    leitura = Leitura(
        device=corpo.device,
        seq=corpo.seq,
        uptime_s=corpo.uptime_s,
        recebido_em=agora_utc(),
        dados=corpo.model_dump(mode="json"),
    )
    s.add(leitura)
    s.commit()

    if anterior is not None and corpo.seq > anterior + 1:
        log.warning("  !! faltaram %d envio(s) de %s (seq %d a %d)", corpo.seq - anterior - 1, corpo.device, anterior + 1, corpo.seq - 1)
    elif anterior is not None and corpo.seq <= anterior:
        log.info("  -- %s recomeçou a sequência (a placa reiniciou?)", corpo.device)
    log.info("%s", _resumo(corpo))
    return {"ok": True, "seq": corpo.seq}


def _resumo(d: LeituraDaPlaca) -> str:
    l = d.leitura
    medida = f"V={l.v} I={l.i} P={l.p} W f={l.f} FP={l.fp}" if l else "sem leitura do PZEM"
    anomalias = ",".join(d.anomalias) or "-"
    return (
        f"{d.device} seq={d.seq} t={d.uptime_s}s | {medida} | dia={d.energia.dia_wh} Wh "
        f"meta={d.meta.estado} | anomalias={anomalias}"
    )


def registrar(app: FastAPI) -> None:
    """Inclui a rota e faz os erros dela saírem como 400 {erro, problemas}, como a placa espera."""
    app.include_router(router)
    padrao = app.exception_handlers.get(RequestValidationError)

    @app.exception_handler(ForaDoContrato)
    async def _fora_do_contrato(request: Request, exc: ForaDoContrato):
        return _recusa(request, exc.problemas)

    @app.exception_handler(RequestValidationError)
    async def _validacao(request: Request, exc: RequestValidationError):
        if request.url.path == ROTA_LEITURAS and request.method == "POST":
            return _recusa(request, [problema_em_portugues(e) for e in exc.errors()])
        return await padrao(request, exc)
