"""Repositório de leituras no SQLite, via SQLAlchemy (implementa RepositorioDeLeituras)."""

from datetime import datetime

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from ..models import Leitura
from .base import LeituraRegistrada, NovaLeitura, ResumoDoDispositivo


def _registro(l: Leitura) -> LeituraRegistrada:
    return LeituraRegistrada(
        id=l.id, device=l.device, seq=l.seq, uptime_s=l.uptime_s, recebido_em=l.recebido_em, dados=l.dados
    )


class RepositorioSQLDeLeituras:
    def __init__(self, sessao: Session):
        self._s = sessao

    def adicionar(self, nova: NovaLeitura) -> LeituraRegistrada:
        linha = Leitura(
            device=nova.device, seq=nova.seq, uptime_s=nova.uptime_s, recebido_em=nova.recebido_em, dados=nova.dados
        )
        self._s.add(linha)
        self._s.commit()
        return _registro(linha)

    def ultimo_seq(self, device: str) -> int | None:
        return self._s.scalar(
            select(Leitura.seq).where(Leitura.device == device).order_by(Leitura.id.desc()).limit(1)
        )

    def ultima(self, device: str) -> LeituraRegistrada | None:
        linha = self._s.scalar(
            select(Leitura)
            .where(Leitura.device == device)
            .order_by(Leitura.recebido_em.desc(), Leitura.id.desc())
            .limit(1)
        )
        return _registro(linha) if linha else None

    def historico(
        self, device: str, desde: datetime | None, ate: datetime | None, limite: int
    ) -> list[LeituraRegistrada]:
        consulta = select(Leitura).where(Leitura.device == device)
        if desde is not None:
            consulta = consulta.where(Leitura.recebido_em >= desde)
        if ate is not None:
            consulta = consulta.where(Leitura.recebido_em <= ate)
        # As mais recentes do período, devolvidas da mais antiga para a mais nova.
        recentes = self._s.scalars(
            consulta.order_by(Leitura.recebido_em.desc(), Leitura.id.desc()).limit(limite)
        ).all()
        return [_registro(l) for l in reversed(recentes)]

    def resumo_por_dispositivo(self) -> list[ResumoDoDispositivo]:
        linhas = self._s.execute(
            select(Leitura.device, func.count(), func.min(Leitura.recebido_em), func.max(Leitura.recebido_em))
            .group_by(Leitura.device)
            .order_by(Leitura.device)
        ).all()
        return [ResumoDoDispositivo(device=d, leituras=n, primeira_em=p, ultima_em=u) for d, n, p, u in linhas]

    def contar(self) -> int:
        return self._s.scalar(select(func.count()).select_from(Leitura)) or 0
