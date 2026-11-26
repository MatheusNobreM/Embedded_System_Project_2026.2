"""US-012: subir o backend no PC com um comando só (processo de verdade, porta de verdade)."""

import os
import re
import socket
import sqlite3
import subprocess
import sys
import threading
import time
from pathlib import Path

import httpx
import pytest

PASTA_BACKEND = Path(__file__).resolve().parent.parent


def ambiente_utf8() -> dict[str, str]:
    """O processo filho escreve em UTF-8 (sem isso, no Windows sai no código de página do console)."""
    return {**os.environ, "PYTHONUNBUFFERED": "1", "PYTHONIOENCODING": "utf-8"}


def porta_livre() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture
def backend_rodando(tmp_path):
    """Roda `python -m energytrack_backend --porta P --banco X` e espera responder."""
    porta = porta_livre()
    banco = tmp_path / "subiu.db"
    proc = subprocess.Popen(
        [sys.executable, "-m", "energytrack_backend", "--porta", str(porta), "--banco", str(banco)],
        cwd=PASTA_BACKEND,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=ambiente_utf8(),
    )
    linhas: list[str] = []
    leitor = threading.Thread(target=lambda: linhas.extend(proc.stdout), daemon=True)
    leitor.start()
    base = f"http://127.0.0.1:{porta}"
    try:
        limite = time.monotonic() + 30
        while True:
            assert proc.poll() is None, "o backend parou ao subir:\n" + "".join(linhas)
            try:
                if httpx.get(f"{base}/", timeout=1).status_code == 200:
                    break
            except httpx.TransportError:
                pass
            assert time.monotonic() < limite, "o backend não respondeu em 30 s:\n" + "".join(linhas)
            time.sleep(0.2)
        yield {"porta": porta, "banco": banco, "base": base, "saida": linhas}
    finally:
        proc.terminate()
        proc.wait(timeout=10)


@pytest.mark.ac("AC-035")
def test_um_comando_sobe_o_backend_e_mostra_a_url(backend_rodando):
    porta, base, saida = backend_rodando["porta"], backend_rodando["base"], "".join(backend_rodando["saida"])

    # A URL para o menuconfig, com o IP do PC (ou o aviso de que não há rede).
    urls = re.findall(rf"http://(\d+\.\d+\.\d+\.\d+|IP-DO-PC):{porta}/api/leituras", saida)
    assert urls, f"o backend não imprimiu a URL para o menuconfig:\n{saida}"

    # Escuta em todas as interfaces: responde pelo loopback e pelo IP impresso.
    for ip in [u for u in urls if u != "IP-DO-PC"]:
        assert httpx.get(f"http://{ip}:{porta}/", timeout=5).status_code == 200, f"não respondeu em {ip}"

    # Migrações aplicadas no banco pedido.
    assert backend_rodando["banco"].exists(), "o arquivo do banco não foi criado"
    with sqlite3.connect(backend_rodando["banco"]) as con:
        versao = con.execute("select version_num from alembic_version").fetchone()
    assert versao is not None, "o banco não tem versão do Alembic"

    # Documentação da API para quem for fazer o app.
    docs = httpx.get(f"{base}/docs", timeout=5)
    assert docs.status_code == 200 and "swagger" in docs.text.lower()
    assert httpx.get(f"{base}/openapi.json", timeout=5).json()["info"]["title"] == "EnergyTrack backend"

    # CORS: um app web em outra origem pode chamar a API.
    origem = "http://localhost:5173"
    preflight = httpx.options(
        f"{base}/api/dispositivos",
        headers={"Origin": origem, "Access-Control-Request-Method": "GET"},
        timeout=5,
    )
    assert preflight.status_code == 200
    assert preflight.headers.get("access-control-allow-origin") in ("*", origem)
    simples = httpx.get(f"{base}/", headers={"Origin": origem}, timeout=5)
    assert simples.headers.get("access-control-allow-origin") in ("*", origem)


@pytest.mark.ac("AC-035")
def test_porta_padrao_8080_e_porta_invalida_recusada():
    from energytrack_backend.config import PORTA_PADRAO

    assert PORTA_PADRAO == 8080
    r = subprocess.run(
        [sys.executable, "-m", "energytrack_backend", "--porta", "70000"],
        cwd=PASTA_BACKEND,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
        env=ambiente_utf8(),
    )
    assert r.returncode != 0
    assert "porta inválida" in r.stderr
