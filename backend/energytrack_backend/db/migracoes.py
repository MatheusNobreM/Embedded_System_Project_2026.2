"""Migrações do Alembic chamadas pelo código.

O banco é criado e atualizado SÓ pelas migrações (alembic/versions), nunca
pela criação automática de tabelas do SQLAlchemy: assim o esquema tem versão
e pode mudar depois sem apagar os dados.
"""

from pathlib import Path

from alembic import command
from alembic.config import Config
from alembic.script import ScriptDirectory

from ..core.config import PASTA_BACKEND


def config_alembic(url: str) -> Config:
    cfg = Config(str(PASTA_BACKEND / "alembic.ini"))
    cfg.set_main_option("script_location", str(PASTA_BACKEND / "alembic"))
    cfg.set_main_option("sqlalchemy.url", url.replace("%", "%%"))
    # O log do alembic.ini é para a linha de comando; aqui quem imprime é o backend.
    cfg.attributes["configurar_log"] = False
    return cfg


def aplicar_migracoes(url: str) -> str:
    """Leva o banco até a migração mais nova (cria o arquivo se faltar). Devolve a versão."""
    if url.startswith("sqlite:///"):
        Path(url.removeprefix("sqlite:///")).parent.mkdir(parents=True, exist_ok=True)
    cfg = config_alembic(url)
    command.upgrade(cfg, "head")
    return ScriptDirectory.from_config(cfg).get_current_head()
