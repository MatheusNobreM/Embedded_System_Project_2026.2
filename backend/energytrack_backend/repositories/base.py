"""O que os serviços esperam de um repositório de leituras, sem depender do SQLAlchemy.

Os registros são dataclasses do domínio (não objetos ORM): o serviço não
sabe que existe um banco, e o teste pode usar um repositório em memória.
Horas sempre em UTC sem fuso.
"""

from dataclasses import dataclass
from datetime import datetime
from typing import Any, Protocol


@dataclass(frozen=True)
class NovaLeitura:
    device: str
    seq: int
    uptime_s: int
    recebido_em: datetime
    dados: dict[str, Any]


@dataclass(frozen=True)
class LeituraRegistrada:
    id: int
    device: str
    seq: int
    uptime_s: int
    recebido_em: datetime
    dados: dict[str, Any]


@dataclass(frozen=True)
class ResumoDoDispositivo:
    device: str
    leituras: int
    primeira_em: datetime
    ultima_em: datetime


class RepositorioDeLeituras(Protocol):
    def adicionar(self, nova: NovaLeitura) -> LeituraRegistrada:
        """Guarda a leitura (e confirma a gravação)."""
        ...

    def ultimo_seq(self, device: str) -> int | None:
        """seq da última leitura guardada da placa (a última inserida), ou None."""
        ...

    def ultima(self, device: str) -> LeituraRegistrada | None:
        """Leitura que chegou por último da placa, ou None."""
        ...

    def historico(
        self, device: str, desde: datetime | None, ate: datetime | None, limite: int
    ) -> list[LeituraRegistrada]:
        """As `limite` leituras mais recentes com desde <= recebido_em <= ate, em ordem cronológica."""
        ...

    def resumo_por_dispositivo(self) -> list[ResumoDoDispositivo]:
        """Uma entrada por placa, em ordem de device."""
        ...

    def contar(self) -> int: ...
