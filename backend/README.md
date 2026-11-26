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

Para mudar o esquema: altere o modelo em `energytrack_backend/banco.py` e gere
a migração (dentro de `backend/`):

```bash
uv run alembic revision --autogenerate -m "o que mudou"
uv run alembic upgrade head        # ou só suba o backend
uv run alembic check               # confere se modelo e migrações batem
```

## Estrutura

```text
pyproject.toml, uv.lock            projeto e dependências travadas (uv)
alembic.ini, alembic/              migrações (versions/0001_leituras.py cria a tabela)
energytrack_backend/
  __main__.py                      comando energytrack-backend (porta, banco, URL para a placa)
  principal.py                     monta o FastAPI: migrações, CORS e rotas
  config.py                        porta padrão, caminho do banco, rota da placa
  banco.py                         tabela leituras, conexão, horas em UTC, migrações
  contrato.py                      o JSON da placa em Pydantic estrito e as mensagens de erro
  rotas_leituras.py                POST /api/leituras
  rotas_consulta.py                consultas do app
tests/                             pytest (um marcador ac("AC-xxx") por critério de aceite)
tests/backend-api.spec.test.mjs    ponte para o onp-spec (roda o pytest com os JSON do código em C da placa)
```

## Testes

```bash
uv run pytest            # dentro de backend/
```

Os testes sobem o backend de verdade numa porta livre, mandam os JSON da placa
e conferem banco, recusas e consultas. Pela raiz do repositório, o `verify`
do onp-spec roda o mesmo pytest pela ponte. A ponte gera os JSON pelo teste em
C da telemetria do firmware, então precisa do `gcc` no PATH, como os testes
do firmware.
