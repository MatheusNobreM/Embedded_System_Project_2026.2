from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """Base das tabelas ORM (models/). O esquema muda só por migração do Alembic."""
