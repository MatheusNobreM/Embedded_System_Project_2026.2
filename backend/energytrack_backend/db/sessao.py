"""Conexão com o SQLite e a fábrica de sessões."""

from sqlalchemy import Engine, create_engine
from sqlalchemy.orm import Session, sessionmaker


def criar_engine(url: str) -> Engine:
    # O FastAPI roda as rotas síncronas em threads: a conexão precisa poder trocar de thread.
    return create_engine(url, connect_args={"check_same_thread": False})


def criar_fabrica_de_sessoes(engine: Engine) -> sessionmaker[Session]:
    return sessionmaker(engine, expire_on_commit=False)
