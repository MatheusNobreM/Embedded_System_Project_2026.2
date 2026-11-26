"""Receber uma leitura da placa (US-010): guardar com a hora de chegada e notar envios perdidos."""

import logging
from dataclasses import dataclass

from ..core.relogio import Relogio
from ..repositories.base import NovaLeitura, RepositorioDeLeituras
from ..schemas.telemetria import LeituraDaPlaca

LOG = logging.getLogger("energytrack_backend")


@dataclass(frozen=True)
class ResultadoDoRecebimento:
    seq: int
    envios_perdidos: int  # buraco no seq desde a leitura anterior da mesma placa
    reiniciou: bool  # o seq voltou: a placa reiniciou


class ServicoDeTelemetria:
    def __init__(self, repositorio: RepositorioDeLeituras, relogio: Relogio, log: logging.Logger = LOG):
        self._repo = repositorio
        self._relogio = relogio
        self._log = log

    def receber(self, leitura: LeituraDaPlaca) -> ResultadoDoRecebimento:
        anterior = self._repo.ultimo_seq(leitura.device)
        self._repo.adicionar(
            NovaLeitura(
                device=leitura.device,
                seq=leitura.seq,
                uptime_s=leitura.uptime_s,
                recebido_em=self._relogio.agora(),
                dados=leitura.model_dump(mode="json"),
            )
        )

        perdidos = leitura.seq - anterior - 1 if anterior is not None and leitura.seq > anterior + 1 else 0
        reiniciou = anterior is not None and leitura.seq <= anterior
        if perdidos:
            self._log.warning(
                "  !! faltaram %d envio(s) de %s (seq %d a %d)", perdidos, leitura.device, anterior + 1, leitura.seq - 1
            )
        elif reiniciou:
            self._log.info("  -- %s recomeçou a sequência (a placa reiniciou?)", leitura.device)
        self._log.info("%s", resumo_da_leitura(leitura))
        return ResultadoDoRecebimento(seq=leitura.seq, envios_perdidos=perdidos, reiniciou=reiniciou)


def resumo_da_leitura(d: LeituraDaPlaca) -> str:
    """A linha que o console mostra a cada leitura."""
    l = d.leitura
    medida = f"V={l.v} I={l.i} P={l.p} W f={l.f} FP={l.fp}" if l else "sem leitura do PZEM"
    anomalias = ",".join(d.anomalias) or "-"
    return (
        f"{d.device} seq={d.seq} t={d.uptime_s}s | {medida} | dia={d.energia.dia_wh} Wh "
        f"meta={d.meta.estado} | anomalias={anomalias}"
    )
