"""Consultas do app (US-011): placas, leitura atual e histórico."""

from datetime import datetime

from ..core.erros import LimiteInvalido, NaoEncontrado
from ..core.relogio import hora_iso, para_utc_sem_fuso
from ..repositories.base import LeituraRegistrada, RepositorioDeLeituras
from ..schemas.leituras import Dispositivo, LeituraGuardada

LIMITE_PADRAO = 720  # 1 h de leituras a cada 5 s
LIMITE_MAXIMO = 5000


def _guardada(l: LeituraRegistrada) -> LeituraGuardada:
    return LeituraGuardada(recebido_em=hora_iso(l.recebido_em), dados=l.dados)


class ServicoDeConsultas:
    def __init__(self, repositorio: RepositorioDeLeituras):
        self._repo = repositorio

    def listar_dispositivos(self) -> list[Dispositivo]:
        return [
            Dispositivo(
                device=r.device, leituras=r.leituras, primeira_em=hora_iso(r.primeira_em), ultima_em=hora_iso(r.ultima_em)
            )
            for r in self._repo.resumo_por_dispositivo()
        ]

    def ultima(self, device: str) -> LeituraGuardada:
        leitura = self._repo.ultima(device)
        if leitura is None:
            raise NaoEncontrado(f"a placa {device} nunca mandou leituras")
        return _guardada(leitura)

    def historico(
        self,
        device: str,
        desde: datetime | None = None,
        ate: datetime | None = None,
        limite: int = LIMITE_PADRAO,
    ) -> list[LeituraGuardada]:
        """Leituras do período (datas sem fuso = UTC), as `limite` mais recentes, em ordem cronológica."""
        if not 1 <= limite <= LIMITE_MAXIMO:
            raise LimiteInvalido(f"limite precisa estar entre 1 e {LIMITE_MAXIMO} (veio {limite})")
        registros = self._repo.historico(
            device,
            para_utc_sem_fuso(desde) if desde is not None else None,
            para_utc_sem_fuso(ate) if ate is not None else None,
            limite,
        )
        return [_guardada(l) for l in registros]

    def total_de_leituras(self) -> int:
        return self._repo.contar()
