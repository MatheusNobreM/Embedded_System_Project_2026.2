"""Monta a aplicação FastAPI: banco migrado, CORS e as rotas."""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import func, select
from sqlalchemy.orm import sessionmaker

from . import __version__, rotas_consulta, rotas_leituras
from .banco import Leitura, aplicar_migracoes, criar_engine
from .config import ROTA_LEITURAS

DESCRICAO = """
Recebe as leituras que a placa do EnergyTrack manda pelo Wi-Fi a cada 5 s
(`POST /api/leituras`, o JSON do contrato da telemetria) e serve a API de
consulta para o app: placas, leitura atual e histórico.
"""


def criar_app(url_banco: str) -> FastAPI:
    """Aplica as migrações no banco da URL e devolve a aplicação pronta para servir."""
    versao_banco = aplicar_migracoes(url_banco)
    engine = criar_engine(url_banco)

    app = FastAPI(title="EnergyTrack backend", version=__version__, description=DESCRICAO)
    app.state.engine = engine
    app.state.sessoes = sessionmaker(engine, expire_on_commit=False)
    app.state.versao_banco = versao_banco

    # O app (web ou celular) ainda não existe e pode rodar em qualquer origem.
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.get("/", summary="Resumo do backend", tags=["sistema"])
    def resumo() -> dict:
        with app.state.sessoes() as s:
            total = s.scalar(select(func.count()).select_from(Leitura))
        return {
            "servico": "EnergyTrack backend",
            "versao": __version__,
            "versao_banco": versao_banco,
            "leituras": total,
            "rota_da_placa": ROTA_LEITURAS,
            "docs": "/docs",
        }

    rotas_leituras.registrar(app)
    rotas_consulta.registrar(app)
    return app
