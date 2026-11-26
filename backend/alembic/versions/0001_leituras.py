"""tabela leituras: cada JSON recebido da placa

Revision ID: 0001
Revises:
Create Date: 2026-11-25
"""

from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op

revision: str = "0001"
down_revision: str | Sequence[str] | None = None
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "leituras",
        sa.Column("id", sa.Integer(), primary_key=True),
        sa.Column("device", sa.String(length=23), nullable=False),
        sa.Column("seq", sa.Integer(), nullable=False),
        sa.Column("uptime_s", sa.Integer(), nullable=False),
        sa.Column("recebido_em", sa.DateTime(), nullable=False),
        sa.Column("dados", sa.JSON(), nullable=False),
    )
    op.create_index("ix_leituras_device_recebido_em", "leituras", ["device", "recebido_em"])


def downgrade() -> None:
    op.drop_index("ix_leituras_device_recebido_em", table_name="leituras")
    op.drop_table("leituras")
