"""AC-038: a reorganização em camadas não muda a API que a placa e o app veem.

tests/openapi_rotas.json é o retrato das rotas (métodos, parâmetros e códigos
de resposta) tirado ANTES da reorganização. Mudou a API de propósito? Gere o
retrato de novo com `uv run python tests/test_contrato_api.py > tests/openapi_rotas.json`
e explique a mudança na spec.
"""

import json
import sys
from pathlib import Path

import pytest

from energytrack_backend.app import criar_app
from energytrack_backend.core.config import url_do_banco

RETRATO = Path(__file__).with_name("openapi_rotas.json")


def retrato_das_rotas(spec: dict) -> dict:
    rotas = {}
    for caminho, metodos in spec["paths"].items():
        for metodo, op in metodos.items():
            parametros = []
            for p in op.get("parameters", []):
                esquema = p.get("schema", {})
                item = {"nome": p["name"], "em": p["in"], "obrigatorio": p.get("required", False)}
                item.update({k: v for k, v in esquema.items() if k in ("default", "minimum", "maximum", "type", "format")})
                if "anyOf" in esquema:
                    item["tipos"] = sorted(x.get("type", "") + ":" + x.get("format", "") for x in esquema["anyOf"])
                parametros.append(item)
            rotas[f"{metodo.upper()} {caminho}"] = {
                "parametros": sorted(parametros, key=lambda p: p["nome"]),
                "respostas": sorted(op["responses"]),
                "corpo": "requestBody" in op,
            }
    return rotas


@pytest.mark.ac("AC-038")
def test_as_rotas_da_api_sao_as_mesmas_de_antes_das_camadas(banco):
    app = criar_app(url_do_banco(banco))
    atual = retrato_das_rotas(app.openapi())
    app.state.engine.dispose()
    assert atual == json.loads(RETRATO.read_text(encoding="utf-8"))


if __name__ == "__main__":
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        app = criar_app(url_do_banco(Path(d) / "retrato.db"))
        json.dump(retrato_das_rotas(app.openapi()), sys.stdout, ensure_ascii=False, indent=2, sort_keys=True)
        app.state.engine.dispose()
