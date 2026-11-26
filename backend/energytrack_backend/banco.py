"""Tabela das leituras, conexão com o SQLite e as migrações do Alembic.

O banco é criado e atualizado SÓ pelas migrações (alembic/versions), nunca
por Base.metadata.create_all: assim o esquema tem versão e pode mudar depois
sem apagar os dados.
"""

from collections.abc import Iterator
from datetime import UTC, datetime
from pathlib import Path

from fastapi import Request
from sqlalchemy import JSON, DateTime, Engine, Index, Integer, String, create_engine
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column

from .config import PASTA_BACKEND


class Base(DeclarativeBase):
    pass


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


def agora_utc() -> datetime:
    """Hora atual em UTC, sem fuso, no formato guardado no banco."""
    return datetime.now(UTC).replace(tzinfo=None)


def para_utc_sem_fuso(momento: datetime) -> datetime:
    """Converte uma data com fuso para UTC sem fuso. Sem fuso, já é tratada como UTC."""
    if momento.tzinfo is None:
        return momento
    return momento.astimezone(UTC).replace(tzinfo=None)


def hora_iso(momento: datetime) -> str:
    """UTC em ISO 8601 com milissegundos e "Z" (ex.: 2026-11-25T14:03:05.123Z)."""
    return momento.replace(tzinfo=UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def criar_engine(url: str) -> Engine:
    # O FastAPI roda as rotas síncronas em threads: a conexão precisa poder trocar de thread.
    return create_engine(url, connect_args={"check_same_thread": False})


def config_alembic(url: str):
    from alembic.config import Config

    cfg = Config(str(PASTA_BACKEND / "alembic.ini"))
    cfg.set_main_option("script_location", str(PASTA_BACKEND / "alembic"))
    cfg.set_main_option("sqlalchemy.url", url.replace("%", "%%"))
    # O log do alembic.ini é para a linha de comando; aqui quem imprime é o backend.
    cfg.attributes["configurar_log"] = False
    return cfg


def aplicar_migracoes(url: str) -> str:
    """Leva o banco até a migração mais nova (cria o arquivo se faltar). Devolve a versão."""
    from alembic import command
    from alembic.script import ScriptDirectory

    if url.startswith("sqlite:///"):
        Path(url.removeprefix("sqlite:///")).parent.mkdir(parents=True, exist_ok=True)
    cfg = config_alembic(url)
    command.upgrade(cfg, "head")
    return ScriptDirectory.from_config(cfg).get_current_head()


def sessao(request: Request) -> Iterator[Session]:
    """Dependência do FastAPI: uma sessão do banco por requisição."""
    with request.app.state.sessoes() as s:
        yield s
