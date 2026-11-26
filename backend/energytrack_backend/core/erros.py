"""Erros do domínio. Não sabem nada de HTTP: a camada api traduz cada um num código."""


class ErroDoDominio(Exception):
    def __init__(self, mensagem: str):
        super().__init__(mensagem)
        self.mensagem = mensagem


class NaoEncontrado(ErroDoDominio):
    """O que foi pedido não existe (ex.: placa que nunca mandou leituras). API: 404."""


class LimiteInvalido(ErroDoDominio):
    """Parâmetro de consulta fora da faixa aceita. API: 422."""


class ForaDoContrato(ErroDoDominio):
    """O JSON da placa não segue o contrato. API: 400 com a lista de problemas."""

    def __init__(self, problemas: list[str]):
        super().__init__("fora do contrato")
        self.problemas = problemas
