"""Erros → respostas HTTP.

- ForaDoContrato e erro de validação no POST da placa → 400 {erro, problemas}
  (a placa só olha se é 2xx; os problemas aparecem no console do backend).
- NaoEncontrado → 404 {erro}; LimiteInvalido → 422 {erro}.
- Outros erros de validação (parâmetros das consultas) → 422 padrão do FastAPI.
"""

import logging

from fastapi import FastAPI, Request
from fastapi.exception_handlers import request_validation_exception_handler
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from pydantic_core import ErrorDetails

from ..core.config import ROTA_LEITURAS
from ..core.erros import ForaDoContrato, LimiteInvalido, NaoEncontrado

LOG = logging.getLogger("energytrack_backend")


def registrar_tratadores(app: FastAPI) -> None:
    @app.exception_handler(ForaDoContrato)
    async def _fora_do_contrato(request: Request, exc: ForaDoContrato):
        return _recusa(request, exc.problemas)

    @app.exception_handler(RequestValidationError)
    async def _validacao(request: Request, exc: RequestValidationError):
        if request.url.path == ROTA_LEITURAS and request.method == "POST":
            return _recusa(request, [problema_em_portugues(e) for e in exc.errors()])
        return await request_validation_exception_handler(request, exc)

    @app.exception_handler(NaoEncontrado)
    async def _nao_encontrado(request: Request, exc: NaoEncontrado):
        return JSONResponse(status_code=404, content={"erro": exc.mensagem})

    @app.exception_handler(LimiteInvalido)
    async def _limite_invalido(request: Request, exc: LimiteInvalido):
        return JSONResponse(status_code=422, content={"erro": exc.mensagem})


def _recusa(request: Request, problemas: list[str]) -> JSONResponse:
    origem = request.client.host if request.client else "?"
    LOG.warning("RECUSADO de %s: %s", origem, "; ".join(problemas))
    return JSONResponse(status_code=400, content={"erro": "fora do contrato", "problemas": problemas})


def _caminho(loc: tuple) -> str:
    texto = "corpo"
    for parte in loc:
        if parte == "body":
            continue
        # Em campo que aceita null, o Pydantic põe o nome do tipo no caminho.
        if isinstance(parte, str) and parte[:1].isupper():
            continue
        texto += f"[{parte}]" if isinstance(parte, int) else f".{parte}"
    return texto


def problema_em_portugues(erro: ErrorDetails) -> str:
    """Uma linha por problema, no estilo do backend de teste: '<campo> <o que está errado>'."""
    tipo, loc = erro["type"], erro["loc"]
    if tipo == "json_invalid":
        return f"o corpo não é JSON válido ({erro.get('ctx', {}).get('error', erro['msg'])})"
    campo = _caminho(loc)
    if campo == "corpo" and tipo in ("model_type", "model_attributes_type", "dict_type"):
        return "o corpo precisa ser um objeto JSON"
    veio = f" (veio {erro['input']!r})" if "input" in erro and tipo != "missing" else ""
    ctx = erro.get("ctx", {})
    match tipo:
        case "missing":
            return f"{campo} está faltando"
        case "extra_forbidden":
            return f"{campo} não faz parte do contrato"
        case "literal_error":
            opcoes = str(ctx.get("expected", "")).replace(" or ", " ou ")
            return f"{campo} precisa ser um de {opcoes}{veio}"
        case "int_type" | "int_from_float":
            return f"{campo} precisa ser um inteiro{veio}"
        case "float_type" | "finite_number":
            return f"{campo} precisa ser um número{veio}"
        case "greater_than_equal":
            return f"{campo} precisa ser >= {ctx.get('ge')}{veio}"
        case "bool_type":
            return f"{campo} precisa ser true ou false{veio}"
        case "string_type" | "string_pattern_mismatch":
            return f"{campo} precisa ser texto com letras, dígitos, - ou _ (até 23){veio}"
        case "list_type":
            return f"{campo} precisa ser uma lista{veio}"
        case "model_type" | "model_attributes_type" | "dict_type":
            return f"{campo} precisa ser um objeto{veio}"
        case "value_error":
            return f"{campo} {erro['msg'].removeprefix('Value error, ')}"
    return f"{campo}: {erro['msg']}{veio}"
