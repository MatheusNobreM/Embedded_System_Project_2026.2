"""Hora do backend: sempre UTC.

O banco guarda UTC sem fuso (o SQLite não guarda fuso) e a API devolve
ISO 8601 com "Z". Quem precisa da hora recebe um Relogio, assim os testes
podem fixar a hora sem mexer no relógio do sistema.
"""

from datetime import UTC, datetime
from typing import Protocol


class Relogio(Protocol):
    def agora(self) -> datetime:
        """Hora atual em UTC, sem fuso."""
        ...


class RelogioDoSistema:
    def agora(self) -> datetime:
        return datetime.now(UTC).replace(tzinfo=None)


def para_utc_sem_fuso(momento: datetime) -> datetime:
    """Converte uma data com fuso para UTC sem fuso. Sem fuso, já é tratada como UTC."""
    if momento.tzinfo is None:
        return momento
    return momento.astimezone(UTC).replace(tzinfo=None)


def hora_iso(momento: datetime) -> str:
    """UTC em ISO 8601 com milissegundos e "Z" (ex.: 2026-11-25T14:03:05.123Z)."""
    return momento.replace(tzinfo=UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")
