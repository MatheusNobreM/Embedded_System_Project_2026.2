"""Contrato do JSON que a placa manda (spec telemetria-wifi, README do firmware seção 6).

Mesmas regras de problemasDoContrato() do backend de teste
(energytrack-fw-3.2/tools/backend-teste.mjs): campos exatos (nem faltando nem
a mais), números >= 0 e sem conversão de texto, nomes de estado e de
anomalia fechados. Erros viram mensagens em português com o caminho do campo
("corpo.meta.estado").
"""

from typing import Annotated, Literal

from pydantic import AfterValidator, BaseModel, ConfigDict, Field
from pydantic_core import ErrorDetails

EstadoMeta = Literal["OK", "PROJECAO_ACIMA", "ATENCAO", "ESTOURADA"]
Anomalia = Literal["SUBTENSAO", "SOBRETENSAO", "FREQUENCIA", "FP_BAIXO", "SALTO_POTENCIA", "SENSOR_SEM_RESPOSTA"]
FaixaTensao = Literal["adequada", "precaria", "critica"]

Numero = Annotated[float, Field(ge=0)]
Inteiro = Annotated[int, Field(ge=0)]


class _Contrato(BaseModel):
    # strict: "221.3" não vira 221.3, 1 não vira true. extra: campo a mais é erro.
    model_config = ConfigDict(strict=True, extra="forbid", allow_inf_nan=False)


class LeituraPzem(_Contrato):
    v: Numero = Field(description="tensão (V)")
    i: Numero = Field(description="corrente (A)")
    p: Numero = Field(description="potência ativa (W)")
    f: Numero = Field(description="frequência (Hz)")
    fp: Numero = Field(description="fator de potência")
    energia_pzem_wh: Inteiro = Field(description="contador de energia do PZEM (Wh)")


class Energia(_Contrato):
    dia: Inteiro = Field(description="dia desde o boot (1, 2...)")
    dia_wh: Numero = Field(description="energia do dia (Wh)")
    anterior_wh: Numero = Field(description="energia do dia anterior (Wh)")
    total_wh: Numero = Field(description="energia desde o boot (Wh)")
    pmed_w: Numero = Field(description="potência média do dia (W)")
    lacunas_recuperadas: Inteiro


class Meta(_Contrato):
    estado: EstadoMeta
    uso_pct: Inteiro
    projecao_wh: Numero
    alerta_potencia: bool


class ContadoresPzem(_Contrato):
    ok: Inteiro
    falha: Inteiro


def _sem_repetidos(nomes: list[str]) -> list[str]:
    if len(set(nomes)) != len(nomes):
        raise ValueError("tem nomes repetidos")
    return nomes


class LeituraDaPlaca(_Contrato):
    """O JSON que a placa manda a cada 5 s em POST /api/leituras."""

    device: str = Field(pattern=r"^[A-Za-z0-9_-]{1,23}$", description='"et-" + MAC do Wi-Fi')
    seq: int = Field(ge=1, description="sobe 1 a cada ciclo de envio; buraco = envio perdido")
    uptime_s: Inteiro = Field(description="segundos desde o boot da placa")
    leitura: LeituraPzem | None = Field(description="null enquanto o PZEM não respondeu nenhuma vez")
    energia: Energia
    meta: Meta
    anomalias: Annotated[list[Anomalia], AfterValidator(_sem_repetidos)]
    tensao_faixa: FaixaTensao
    saltos: Inteiro
    pzem: ContadoresPzem


# ------------------------------------------------------------ mensagens --
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
