"""Tabelas ORM. Importar este pacote registra todas elas em Base.metadata (o Alembic usa)."""

from .leitura import Leitura

__all__ = ["Leitura"]
