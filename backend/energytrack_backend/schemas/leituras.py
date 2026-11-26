"""DTOs de saída da API: o que a placa e o app recebem de volta."""

from typing import Any

from pydantic import BaseModel, Field


class RecebimentoOk(BaseModel):
    ok: bool = True
    seq: int = Field(description="o seq da leitura guardada")


class ErroDeContrato(BaseModel):
    erro: str = Field(examples=["fora do contrato"])
    problemas: list[str] = Field(examples=[["corpo.meta.estado precisa ser um de 'OK', 'PROJECAO_ACIMA', 'ATENCAO' ou 'ESTOURADA' (veio 'BOM')"]])


class Erro(BaseModel):
    erro: str


class Dispositivo(BaseModel):
    device: str = Field(description='identificador da placa ("et-" + MAC)')
    leituras: int = Field(description="quantas leituras ela já mandou")
    primeira_em: str = Field(description="hora da primeira leitura (UTC, ISO 8601)")
    ultima_em: str = Field(description="hora da última leitura (UTC, ISO 8601)")


class LeituraGuardada(BaseModel):
    recebido_em: str = Field(description="hora de chegada no backend (UTC, ISO 8601)", examples=["2026-11-25T14:03:05.123Z"])
    dados: dict[str, Any] = Field(description="o JSON da placa como foi recebido (ver POST /api/leituras)")
