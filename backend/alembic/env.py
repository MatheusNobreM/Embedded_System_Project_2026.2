"""Ambiente das migrações: usa as tabelas de models/ e o banco de core/config.py."""

from logging.config import fileConfig
from pathlib import Path

from alembic import context
from sqlalchemy import create_engine

import energytrack_backend.models  # noqa: F401  (registra as tabelas em Base.metadata)
from energytrack_backend.db.base import Base
from energytrack_backend.core.config import caminho_do_banco, url_do_banco

config = context.config
if config.config_file_name is not None and config.attributes.get("configurar_log", True):
    fileConfig(config.config_file_name, disable_existing_loggers=False)

target_metadata = Base.metadata


def _url() -> str:
    return config.get_main_option("sqlalchemy.url") or url_do_banco(caminho_do_banco())


def run_migrations_offline() -> None:
    context.configure(
        url=_url(),
        target_metadata=target_metadata,
        literal_binds=True,
        render_as_batch=True,
    )
    with context.begin_transaction():
        context.run_migrations()


def run_migrations_online() -> None:
    url = _url()
    if url.startswith("sqlite:///"):
        Path(url.removeprefix("sqlite:///")).parent.mkdir(parents=True, exist_ok=True)
    engine = create_engine(url)
    try:
        with engine.connect() as connection:
            # render_as_batch: o SQLite não altera colunas; o Alembic recria a tabela.
            context.configure(connection=connection, target_metadata=target_metadata, render_as_batch=True)
            with context.begin_transaction():
                context.run_migrations()
    finally:
        engine.dispose()


if context.is_offline_mode():
    run_migrations_offline()
else:
    run_migrations_online()
