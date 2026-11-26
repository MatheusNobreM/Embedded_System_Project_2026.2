"""Monta a aplicação FastAPI: banco migrado, sessões, CORS, erros e rotas.

Camadas (as dependências só descem; o princípio P-006 da constituição confere):
    api/routes → services → repositories → models/db
    schemas (DTOs) e core (config, relógio, erros) servem a todas.
"""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from . import __version__
from .api.erros import registrar_tratadores
from .api.routes import dispositivos, leituras, sistema
from .db.migracoes import aplicar_migracoes
from .db.sessao import criar_engine, criar_fabrica_de_sessoes

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
    app.state.sessoes = criar_fabrica_de_sessoes(engine)
    app.state.versao_banco = versao_banco

    # O app (web ou celular) ainda não existe e pode rodar em qualquer origem.
    app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

    registrar_tratadores(app)
    app.include_router(sistema.router)
    app.include_router(leituras.router)
    app.include_router(dispositivos.router)
    return app
