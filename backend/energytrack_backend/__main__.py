"""Sobe o backend no PC: `uv run energytrack-backend [--porta 8080] [--banco arquivo.db]`."""

import argparse
import socket

import uvicorn

from .config import PORTA_PADRAO, ROTA_LEITURAS, caminho_do_banco, url_do_banco
from .principal import criar_app


def enderecos_locais() -> list[str]:
    """IPv4 do PC fora do loopback: um deles é o que vai na URL da placa."""
    ips: set[str] = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    # IP da rota padrão (não manda pacote nenhum: UDP só escolhe a interface).
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            ips.add(s.getsockname()[0])
    except OSError:
        pass
    return sorted(ip for ip in ips if not ip.startswith("127.") and ip != "0.0.0.0")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="energytrack-backend",
        description="Backend do EnergyTrack: recebe as leituras da placa e serve a API do app.",
    )
    parser.add_argument("--porta", type=int, default=PORTA_PADRAO, help=f"porta HTTP (padrão {PORTA_PADRAO})")
    parser.add_argument("--banco", help="arquivo SQLite (padrão: ET_BANCO ou backend/dados/energytrack.db)")
    args = parser.parse_args(argv)
    if not 1 <= args.porta <= 65535:
        parser.error(f"porta inválida: {args.porta}")

    caminho = caminho_do_banco(args.banco)
    app = criar_app(url_do_banco(caminho))

    print(f"EnergyTrack backend na porta {args.porta}")
    print(f"  banco: {caminho} (migrações na versão {app.state.versao_banco})")
    ips = enderecos_locais()
    if not ips:
        print("  nenhum endereço de rede encontrado: o PC está conectado?")
        ips = ["IP-DO-PC"]
    for ip in ips:
        print(f"  URL para o menuconfig (CONFIG_ET_BACKEND_URL): http://{ip}:{args.porta}{ROTA_LEITURAS}")
    print(f"  documentação da API: http://localhost:{args.porta}/docs")
    print("Ctrl+C para parar.", flush=True)

    uvicorn.run(app, host="0.0.0.0", port=args.porta)


if __name__ == "__main__":
    main()
