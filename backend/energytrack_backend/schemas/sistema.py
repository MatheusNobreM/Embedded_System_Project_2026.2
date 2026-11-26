from pydantic import BaseModel


class Resumo(BaseModel):
    servico: str
    versao: str
    versao_banco: str
    leituras: int
    rota_da_placa: str
    docs: str
