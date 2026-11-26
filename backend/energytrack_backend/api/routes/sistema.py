"""GET /: resumo do backend (versão, versão do banco, total de leituras)."""

from fastapi import APIRouter, Request

from ... import __version__
from ...core.config import ROTA_LEITURAS
from ...schemas.sistema import Resumo
from ..deps import Consultas

router = APIRouter(tags=["sistema"])


@router.get("/", summary="Resumo do backend")
def resumo(request: Request, consultas: Consultas) -> Resumo:
    return Resumo(
        servico="EnergyTrack backend",
        versao=__version__,
        versao_banco=request.app.state.versao_banco,
        leituras=consultas.total_de_leituras(),
        rota_da_placa=ROTA_LEITURAS,
        docs="/docs",
    )
