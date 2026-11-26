# EnergyTrack — backend (FastAPI + SQLite)

Recebe as leituras que a placa do EnergyTrack manda pelo Wi-Fi a cada 5 s,
guarda cada uma num banco SQLite e oferece uma API JSON para o app: lista de
placas, leitura atual e histórico de um período. Roda no PC, na mesma rede da
placa, e foi pensado para demonstrações de até 1 h.

A placa não muda nada: ela já manda `POST /api/leituras` com o JSON do
contrato (README do firmware, seção 6). A porta padrão é a mesma do backend de
teste (8080), então a URL do menuconfig continua a mesma.

## Instalar

Precisa só do [uv](https://docs.astral.sh/uv/getting-started/installation/).
Ele baixa o Python 3.12 e as dependências sozinho. No Windows:

```powershell
winget install --id=astral-sh.uv -e
```

## Subir

Dentro de `backend/`:

```bash
uv run energytrack-backend                       # porta 8080, banco em dados/energytrack.db
uv run energytrack-backend --porta 9000          # outra porta
uv run energytrack-backend --banco demo-1.db     # outro arquivo de banco (ou a variável ET_BANCO)
```

Ao subir, ele aplica as migrações do banco e imprime a URL para a placa:

```text
EnergyTrack backend na porta 8080
  banco: ...\backend\dados\energytrack.db (migrações na versão 0001)
  URL para o menuconfig (CONFIG_ET_BACKEND_URL): http://192.168.0.10:8080/api/leituras
  documentação da API: http://localhost:8080/docs
Ctrl+C para parar.
[14:03:05] et-a4cf12345678 seq=121 t=602s | V=221.3 I=1.234 P=271.9 W f=60.0 FP=0.98 | dia=45.312 Wh meta=OK | anomalias=-
```

- Coloque a URL impressa em `CONFIG_ET_BACKEND_URL` (README do firmware,
  seção 6) e grave a placa. O PC e a placa precisam estar na **mesma rede**.
- No Windows, permita o Python no firewall quando ele perguntar (rede
  privada). Sem isso, a placa conta falhas do tipo `rede` na linha `rede:`.
- Não rode o backend de teste (`node tools/backend-teste.mjs`) ao mesmo tempo
  na mesma porta.
- O console mostra uma linha por leitura, `RECUSADO` para um JSON fora do
  contrato e `!! faltaram N envio(s)` quando há buraco no `seq`.

O banco fica em `backend/dados/` (fora do git). Para começar uma
demonstração do zero, pare o backend e apague o arquivo, ou use `--banco`
com outro nome.

## API

Documentação interativa (OpenAPI/Swagger) em `http://localhost:8080/docs`.
O CORS está liberado para qualquer origem, então um app web rodando em outra
porta pode chamar a API direto. Não há autenticação: é para a rede local da
demonstração.

| Rota | O que faz |
|---|---|
| `POST /api/leituras` | a placa entrega uma leitura. `201 {"ok": true, "seq": N}` = guardada; `400 {"erro": "fora do contrato", "problemas": [...]}` = recusada e não guardada |
| `GET /api/dispositivos` | placas que já mandaram dados |
| `GET /api/dispositivos/{device}/ultima` | leitura mais recente da placa (`404 {"erro": ...}` se ela nunca mandou nada) |
| `GET /api/dispositivos/{device}/leituras` | histórico em ordem cronológica. Parâmetros: `desde`, `ate` (ISO 8601; sem fuso = UTC) e `limite` (1 a 5000, padrão 720 = 1 h). Passando do limite, ficam as mais recentes do período |
| `GET /` | resumo: versão, versão do banco e total de leituras |

As horas são sempre UTC em ISO 8601 com `Z`. O app converte para o fuso
local.

`GET /api/dispositivos`:

```json
[
  { "device": "et-a4cf12345678", "leituras": 718, "primeira_em": "2026-11-25T14:00:00.512Z", "ultima_em": "2026-11-25T14:59:50.498Z" }
]
```

`GET /api/dispositivos/et-a4cf12345678/ultima` e cada item do histórico
trazem o JSON da placa como chegou, mais a hora de chegada:

```json
{
  "recebido_em": "2026-11-25T14:59:50.498Z",
  "dados": {
    "device": "et-a4cf12345678", "seq": 720, "uptime_s": 3601,
    "leitura": { "v": 221.3, "i": 1.234, "p": 271.9, "f": 60.0, "fp": 0.98, "energia_pzem_wh": 102 },
    "energia": { "dia": 1, "dia_wh": 271.5, "anterior_wh": 0.0, "total_wh": 271.5, "pmed_w": 271.5, "lacunas_recuperadas": 0 },
    "meta": { "estado": "OK", "uso_pct": 5, "projecao_wh": 6516.0, "alerta_potencia": false },
    "anomalias": [], "tensao_faixa": "adequada", "saltos": 0, "pzem": { "ok": 3600, "falha": 0 }
  }
}
```

Exemplo de gráfico dos últimos 10 min:
`GET /api/dispositivos/et-a4cf12345678/leituras?desde=2026-11-25T14:50:00Z`.

## Banco e migrações

SQLite com SQLAlchemy 2. O esquema muda **só por migração do Alembic**: o
backend aplica `upgrade head` ao subir, e nada cria tabela por fora. A tabela
`leituras` tem `device`, `seq`, `uptime_s`, `recebido_em` (UTC) e `dados` (o
JSON inteiro), com índice por placa e hora.

Para mudar o esquema: altere o modelo em `energytrack_backend/models/` e gere a
migração (dentro de `backend/`):

```bash
uv run alembic revision --autogenerate -m "o que mudou"
uv run alembic upgrade head        # ou só suba o backend
uv run alembic check               # confere se modelo e migrações batem
```

## Arquitetura

O código é dividido em camadas, e as dependências só descem:

```text
          placa / app (HTTP)
                 │
  api/routes     recebe a requisição, chama o serviço, devolve o schema
  api/deps       monta sessão → repositório → serviço (Depends do FastAPI)
  api/erros      erro do domínio → 400 / 404 / 422
                 │
  services       as regras: guardar com a hora de chegada, envios perdidos,
                 limite e período do histórico. Sem FastAPI e sem SQLAlchemy
                 │  (recebe um RepositorioDeLeituras, que é um Protocol)
  repositories   consultas no banco (SQLAlchemy); devolvem dataclasses do domínio
                 │
  models / db    tabela ORM, sessão, migrações do Alembic

  schemas  DTOs Pydantic: entrada (o JSON da placa) e saída (o que o app recebe)
  core     configuração, relógio (UTC) e erros do domínio
```

- Uma rota nunca fala com o banco; um serviço nunca importa FastAPI nem
  SQLAlchemy; um repositório nunca conhece HTTP. O princípio **P-006** da
  constituição do onp-spec confere isso a cada `audit`, e
  `tests/test_servicos.py` confere que os serviços carregam sem essas
  bibliotecas.
- Por isso os serviços são testados com um repositório em memória e um relógio
  fixo (`tests/test_servicos.py`), sem banco nem servidor.

### Onde entra cada mudança

| Quero... | Mexa em |
|---|---|
| uma rota nova para o app | `schemas/` (o DTO de saída), `services/` (a regra), `api/routes/` (a rota) e, se precisar de consulta nova, `repositories/base.py` (o Protocol) + `repositories/leituras_sql.py` |
| uma coluna ou tabela nova | `models/` e uma migração: `uv run alembic revision --autogenerate -m "..."` |
| mudar o contrato do JSON da placa | `schemas/telemetria.py` (e o firmware e o backend de teste, que seguem o mesmo contrato) |
| um erro novo com código HTTP próprio | `core/erros.py` (o erro) e `api/erros.py` (o código HTTP) |
| trocar o banco | só `db/` e `repositories/leituras_sql.py`: os serviços não mudam |

## Estrutura

```text
pyproject.toml, uv.lock            projeto e dependências travadas (uv)
alembic.ini, alembic/              migrações (versions/0001_leituras.py cria a tabela)
energytrack_backend/
  __main__.py                      comando energytrack-backend (porta, banco, URL para a placa)
  app.py                           monta o FastAPI: migrações, CORS, erros e rotas
  api/
    deps.py                        injeção de dependências (sessão, repositório, serviços, relógio)
    erros.py                       erros → HTTP; mensagens dos problemas do contrato em português
    routes/leituras.py             POST /api/leituras
    routes/dispositivos.py         GET /api/dispositivos[/{device}/ultima | /{device}/leituras]
    routes/sistema.py              GET /
  services/
    telemetria.py                  ServicoDeTelemetria: receber leitura, envios perdidos, linha do console
    consultas.py                   ServicoDeConsultas: placas, última leitura, histórico
  repositories/
    base.py                        Protocol RepositorioDeLeituras e os registros do domínio
    leituras_sql.py                implementação com SQLAlchemy
  schemas/
    telemetria.py                  entrada: o JSON da placa em Pydantic estrito
    leituras.py, sistema.py        saídas: LeituraGuardada, Dispositivo, erros, resumo
  models/leitura.py                tabela leituras (ORM)
  db/                              base ORM, sessão, migrações chamadas pelo código
  core/                            config (porta, banco), relógio UTC, erros do domínio
tests/
  test_receber.py, test_consultar.py, test_execucao.py   a API de ponta a ponta (backend-api)
  test_servicos.py                 as regras isoladas, com repositório em memória
  test_contrato_api.py             retrato das rotas (openapi_rotas.json): a API não muda sem querer
  backend-api.spec.test.mjs        ponte para o onp-spec (roda o pytest com os JSON do código em C da placa)
```

## Testes

```bash
uv run pytest            # dentro de backend/
```

Os testes de API sobem o backend de verdade numa porta livre, mandam os JSON
da placa e conferem banco, recusas e consultas; os de serviço rodam as regras
sem banco e sem HTTP. Pela raiz do repositório, o `verify`
do onp-spec roda o mesmo pytest pela ponte. A ponte gera os JSON pelo teste em
C da telemetria do firmware, então precisa do `gcc` no PATH, como os testes
do firmware.
