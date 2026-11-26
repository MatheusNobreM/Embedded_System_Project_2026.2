from datetime import datetime

from sqlalchemy import JSON, DateTime, Index, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from ..db.base import Base


class Leitura(Base):
    """Uma leitura recebida da placa: o JSON inteiro e as colunas usadas nas consultas."""

    __tablename__ = "leituras"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    device: Mapped[str] = mapped_column(String(23))
    seq: Mapped[int] = mapped_column(Integer)
    uptime_s: Mapped[int] = mapped_column(Integer)
    # UTC sem fuso no banco (o SQLite não guarda fuso); a API devolve com "Z".
    recebido_em: Mapped[datetime] = mapped_column(DateTime())
    dados: Mapped[dict] = mapped_column(JSON)

    __table_args__ = (Index("ix_leituras_device_recebido_em", "device", "recebido_em"),)
