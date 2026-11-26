"""POST /api/leituras: a placa entrega cada leitura (US-010)."""

from fastapi import APIRouter, Depends

from ...core.config import ROTA_LEITURAS
from ...schemas.leituras import ErroDeContrato, RecebimentoOk
from ...schemas.telemetria import LeituraDaPlaca
from ..deps import Telemetria, limitar_corpo

router = APIRouter(tags=["placa"])


@router.post(
    ROTA_LEITURAS,
    status_code=201,
    summary="A placa entrega uma leitura",
    response_description="Leitura guardada",
    responses={400: {"model": ErroDeContrato, "description": "Fora do contrato (nada é guardado)"}},
    dependencies=[Depends(limitar_corpo)],
)
def receber_leitura(corpo: LeituraDaPlaca, telemetria: Telemetria) -> RecebimentoOk:
    """Guarda o JSON da placa com a hora de chegada (UTC). 201 = guardado."""
    resultado = telemetria.receber(corpo)
    return RecebimentoOk(seq=resultado.seq)
