"""Onde fica o banco, a porta padrão e as rotas fixas do contrato com a placa."""

import os
from pathlib import Path

PASTA_BACKEND = Path(__file__).resolve().parent.parent
BANCO_PADRAO = PASTA_BACKEND / "dados" / "energytrack.db"
PORTA_PADRAO = 8080

# Rota que a placa usa (CONFIG_ET_BACKEND_URL no menuconfig). A mesma do backend de teste.
ROTA_LEITURAS = "/api/leituras"


def caminho_do_banco(argumento: str | None = None) -> Path:
    """Arquivo SQLite: --banco, senão a variável ET_BANCO, senão backend/dados/energytrack.db."""
    valor = argumento or os.environ.get("ET_BANCO")
    return Path(valor).resolve() if valor else BANCO_PADRAO


def url_do_banco(caminho: Path) -> str:
    return f"sqlite:///{caminho.as_posix()}"
