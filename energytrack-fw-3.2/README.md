# EnergyTrack — firmware do ESP32 (WPS 3.1 e 3.2)

ESP32 DevKit (ESP32-WROOM-32) + sensor PZEM-004T v3 na UART2 (GPIO17/GPIO16),
com o FreeRTOS do ESP-IDF, compilado pelo PlatformIO. Aquisição e
processamento rodam no núcleo 1. O estado aparece no LED da própria placa
(GPIO2). Com o Wi-Fi configurado, uma tarefa no núcleo 0 manda os dados a um
backend em JSON por HTTP POST, a cada 5 s (seção 6).

> Até 05/11/2026 o firmware era da Raspberry Pi Pico W (pico-sdk). Essa versão
> está preservada na branch `pico-w`; esta, do ESP32, vive na branch `esp-32`.
> Em 07/11/2026 saíram o sensor de temperatura LM35 e a aquisição por ADC + DMA.

| Ambiente (PlatformIO) | WPS | O que faz |
|---|---|---|
| `energytrack_fw` | 3.1 | Tarefas FreeRTOS de aquisição (UART disparada por timer de hardware) e processamento local de potência, energia, meta diária e anomalias, com monitor de deadline; telemetria pelo Wi-Fi para o backend (opcional) |
| `energytrack_selftest` | 3.2 | Self-test do PZEM, do LED e do log serial (`>>> SELFTEST PASS <<<`) |

Diagrama de tarefas, prioridades, deadlines e a análise de tempo de resposta:
[docs/diagrama-tarefas.md](docs/diagrama-tarefas.md). Explicação em linguagem
simples: [docs/como-funciona.md](docs/como-funciona.md). Cada pasta, arquivo e
função explicados em detalhe: [docs/guia-do-codigo.md](docs/guia-do-codigo.md).

## Estrutura

```
platformio.ini              dois ambientes (energytrack_fw e energytrack_selftest), ESP-IDF 6.1,
                            partição de 1,5 MB para a aplicação
CMakeLists.txt              projeto ESP-IDF
sdkconfig.defaults          240 MHz, tick de 1 kHz, console na UART0; rede no núcleo 0, sem gravar na flash
include/board_pins.h        mapa de pinos (fonte única; sem dependência do SDK)
include/board_resources.h   UART2/console/GPIO do ESP-IDF + trava contra console na UART do PZEM
include/energy_config.h     metas e limiares (tensão 220/127 V, meta, limite...)
src/CMakeLists.txt          componente principal: escolhe as fontes de cada firmware (ET_APP)
src/Kconfig.projbuild       menu "EnergyTrack - telemetria" do menuconfig (Wi-Fi e URL do backend)
src/hal/                    board_init (console, UART2, LED), pzem_modbus/pzem_link,
                            acq_timer (gptimer de 1 s), wifi_link (Wi-Fi), http_post (POST)
src/proc/                   C puro: energy, goal, anomaly, processing, telemetry (JSON e contadores)
src/rt/                     C puro: rt_tasks (tabela), rt_monitor (deadline), rt_sched (RTA, só nos testes)
app/energytrack_main.c      firmware do WPS 3.1 (com a tarefa net da telemetria)
app/selftest_main.c         self-test do WPS 3.2
test/host/*.c               testes em C para o PC (saída TAP)
test/spec/*.spec.test.mjs   ponte node:test <-> critérios de aceite do onp-spec
test/bench/                 log serial da prova de bancada e o que o backend de teste recebeu
tools/capturar-serial.ps1   captura o log do DevKit pela ponte USB-serial
tools/backend-teste.mjs     backend de teste (Node, sem dependências): recebe e confere o JSON
```

## Ligação

| Sinal | ESP32 DevKit | Observação |
|---|---|---|
| PZEM RX | GPIO17 (UART2 TX) | |
| PZEM TX | GPIO16 (UART2 RX) | o ESP32 não tolera 5 V: veja a nota abaixo |
| PZEM 5V | VIN / 5V (ou 3V3) | o lado TTL do PZEM; VIN só existe com a placa no USB |
| PZEM GND | GND | terra comum |
| LED | GPIO2 | LED azul da própria placa: não precisa ligar nada |

Alimentando o lado TTL do PZEM com 5 V, o TX dele sai em 5 V: use um conversor
de nível (ou um divisor de tensão) até o GPIO16. Muitos PZEM-004T v3 funcionam
com o lado TTL alimentado em 3,3 V (pino 3V3 do DevKit), o que dispensa o
conversor; se o seu não responder assim, volte para 5 V com o conversor.

Não use GPIO6 a GPIO11 (flash), GPIO1/GPIO3 (console) nem GPIO0/GPIO12 (boot).

**Atenção:** o lado de medição do PZEM (bornes de tensão e o transformador de
corrente) fica na rede de 220 V. Faça as ligações com tudo desligado da tomada
e não encoste nos bornes com a rede ligada. O lado TTL é isolado da rede.

## 1. Primeiro teste na placa (com ou sem o transformador de corrente)

Serve para conferir que o firmware roda e conversa com o PZEM antes da bancada
oficial. O transformador de corrente (TC) é opcional aqui: sem ele, tensão e
frequência são medidas normalmente e corrente, potência e energia ficam em 0.

1. **Driver da placa.** O DevKit usa a ponte USB-serial CP2102. Se no
   Gerenciador de Dispositivos ela aparece com um aviso (sem porta COM),
   instale o driver *CP210x Universal Windows Driver* da Silicon Labs
   (silabs.com, "CP210x USB to UART Bridge VCP Drivers"): descompacte, clique
   com o botão direito em `silabser.inf` → **Instalar**, e reconecte a placa.
   Deve surgir "Silicon Labs CP210x ... (COMx)".
2. **Rede no PZEM.** O PZEM-004T v3 é alimentado pelo lado da rede: **sem 220 V
   nos bornes de tensão ele não responde**, mesmo com o lado TTL ligado.
3. **Self-test** (na pasta `energytrack-fw-3.2`):

   ```bash
   pio run -e energytrack_selftest -t upload -t monitor
   ```

   Esperado a cada 2 s: `PZEM ok=` subindo, `V=` perto da tensão da tomada,
   `f=` perto de 60 Hz e, sem o TC, `I=0.000 A P=0.0 W`. LED da placa aceso
   fixo. Depois de 60 s: `>>> SELFTEST PASS <<<`. Se só aparece `falha=`
   subindo e o LED pisca: confira TX/RX cruzados (TX do ESP32 no RX do PZEM),
   o GND comum, a alimentação do lado TTL e os 220 V nos bornes.
4. **Firmware principal:**

   ```bash
   pio run -e energytrack_fw -t upload -t monitor
   ```

   O relatório sai a cada 2 s (exemplo na seção 4). Sem o TC a potência fica
   em 0 W, então meta, limite de potência e fator de potência não disparam (o
   fator de potência só é avaliado com carga de 100 W ou mais). Confira que a
   linha `rt t=...` mostra `miss=0` em `proc` e `pzem`.

Para sair do monitor: `Ctrl+C`. No Windows, se `pio` não estiver no PATH, use
`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.

## 2. Testes no PC (sem placa)

Precisa de `gcc` e `node` (≥ 20). Na raiz do repositório:

```bash
node --test "energytrack-fw-3.2/test/spec/*.spec.test.mjs"
```

Cada critério de aceite da especificação
(`.spec/features/aquisicao-processamento/spec.md`) tem um teste com
`@spec:AC-xxx` no título. Os testes em C cobrem mapa de pinos, energia, meta,
anomalias, LED, monitor de deadline e análise de tempo de resposta. O teste de
compilação (AC-020) chama o PlatformIO (`~/.platformio`). O teste de bancada
(AC-018) só passa depois que o log do DevKit for capturado.

Gate do onp-spec (prova por critério + auditoria):

```bash
node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs verify aquisicao-processamento
node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs audit --ci
```

## 3. Compilar e gravar

Com a extensão **PlatformIO IDE** do VS Code, abra a pasta `energytrack-fw-3.2`
e use **Build** / **Upload** / **Monitor** no ambiente desejado. No terminal
(na pasta `energytrack-fw-3.2`):

```bash
pio run                                   # compila os dois ambientes
pio run -e energytrack_fw -t upload       # grava o firmware do WPS 3.1
pio device monitor                        # console a 115200 bps
```

O primeiro build baixa e compila o ESP-IDF (alguns minutos); os seguintes só
recompilam o projeto. Saem `.pio/build/energytrack_fw/firmware.bin` e
`.pio/build/energytrack_selftest/firmware.bin`. Se a gravação não começar,
segure o botão BOOT ao conectar.

## 4. Bancada do WPS 3.1 (tempo real sem estouro de deadline)

A prova oficial (critério AC-018) é com o PZEM ligado à rede **e** a uma carga
pelo transformador de corrente. Com o Wi-Fi configurado e o backend de teste
rodando (seção 6), a mesma captura também prova o critério AC-028: 10 min
enviando ao backend sem estouro de deadline.

1. Grave `energytrack_fw` (seção 3).
2. PZEM ligado à rede, com o TC abraçando um fio da carga.
3. (Para o AC-028) Em outro terminal: `node tools/backend-teste.mjs`.
4. `powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1` (11 min).
5. Pare o backend de teste (Ctrl+C) e rode o `verify` do onp-spec (seção 2).

O relatório sai pelo console a cada 2 s:

```
[  602s] PZEM ok=601 falha=0 | V=221.3 V I=1.234 A P=271.9 W f=60.0 Hz FP=0.98
  energia: dia 1 (0h10m) dia=45.312 Wh | anterior=0.000 Wh | total=45.312 Wh | Pmed=271.5 W | lacunas recuperadas=0
  meta: uso=0% projecao=6516.000 Wh estado=OK | limite de potencia: ok
  anomalias: nenhuma | saltos=0
  rede: wifi=conectado ip=192.168.0.23 | envios ok=118 falha=2 (wifi=2 rede=0 http=0 json=0) | ultimo=HTTP 201 | seq=120 | envio max=85ms
  rt t=602s proc:jobs=601,miss=0,rmax=310us,d=50000us pzem:jobs=601,miss=0,rmax=48000us,d=500000us ...
  heap livre=... B | pilha livre (bytes): proc=... pzem=... status=... report=... net=...
  >>> RT PASS <<< 600 s sem estouro de deadline
```

LED da placa: aceso fixo = normal; pisca a 1 Hz = PZEM sem resposta; pisca a
5 Hz = anomalia, meta estourada ou limite de potência.

## 5. Bancada do WPS 3.2 (self-test)

Grave `energytrack_selftest` (`pio run -e energytrack_selftest -t upload`),
abra o monitor serial e deixe rodar 60 s com carga. Aceite com
`>>> SELFTEST PASS <<<` (no máximo 5% de leituras com falha). LED aceso fixo =
PZEM respondendo; piscando = sem resposta válida.

## 6. Telemetria: mandar os dados ao backend pelo Wi-Fi

A cada 5 s a tarefa `net`, presa no **núcleo 0**, pega a fotografia mais
recente do processamento e faz um `POST` com um JSON para a URL do backend.
Wi-Fi e pilha TCP/IP também ficam no núcleo 0 e nada da rede grava na flash:
o núcleo 1 (aquisição e processamento) e a prova de tempo real não mudam. Se o
Wi-Fi ou o backend caírem, a placa só conta a falha e tenta de novo no ciclo
seguinte. Sem SSID ou URL configurados (o padrão do repositório), o firmware
roda sem rede e o relatório mostra `rede: desligada (sem SSID)`.

### Configurar o Wi-Fi e a URL

Os valores ficam em `sdkconfig.energytrack_fw`, que o git **não** versiona:
SSID e senha nunca vão para o repositório. Duas formas de preencher:

- **Editando o arquivo.** Depois do primeiro build ele existe; troque as três
  linhas e compile de novo:

  ```text
  CONFIG_ET_WIFI_SSID="REDE"
  CONFIG_ET_WIFI_PASSWORD="SENHA"
  CONFIG_ET_BACKEND_URL="http://192.168.0.10:8080/api/leituras"
  ```

- **Pelo menuconfig:** `pio run -e energytrack_fw -t menuconfig` → menu
  **EnergyTrack - telemetria** (use um terminal comum, fora do VS Code, se a
  tela sair embaralhada).

A rede precisa ser de **2,4 GHz** e WPA2-Personal, ou aberta. O ESP32 não usa
5 GHz, e redes corporativas como a eduroam não funcionam. O hotspot do celular
serve. A URL pode ser `http://` (rede local) ou `https://` (certificado de uma
CA pública).

> Se você apagar o `sdkconfig.energytrack_fw` para ele pegar mudanças do
> `sdkconfig.defaults`, preencha as três linhas de novo. O teste do critério
> AC-026 avisa quando esse arquivo é de antes da rede.

### Backend de teste

Para testar sem o backend de verdade, use o backend de teste, em Node e sem
dependências. No PC conectado à **mesma rede** da placa, dentro de
`energytrack-fw-3.2`:

```bash
node tools/backend-teste.mjs            # porta 8080
```

Ele imprime a URL a configurar (`http://<ip-do-pc>:8080/api/leituras`) e, a
cada envio, uma linha com a leitura. Envios fora do contrato recebem `400` com
a lista de problemas, e um `seq` faltando aparece como `!! faltaram N
envio(s)`. As leituras aceitas vão para `test/bench/telemetria-recebida.jsonl`,
zerado a cada execução (`--arquivo outro.jsonl` grava em outro lugar). No
Windows, permita o Node no firewall quando ele perguntar.

### O JSON enviado

`POST <URL>` com `Content-Type: application/json`. Resposta 2xx = recebido;
qualquer outra conta como falha.

```json
{
  "device": "et-a4cf12345678",
  "seq": 121,
  "uptime_s": 602,
  "leitura": { "v": 221.3, "i": 1.234, "p": 271.9, "f": 60.0, "fp": 0.98, "energia_pzem_wh": 102 },
  "energia": { "dia": 1, "dia_wh": 45.312, "anterior_wh": 0.000, "total_wh": 45.312, "pmed_w": 271.5, "lacunas_recuperadas": 0 },
  "meta": { "estado": "OK", "uso_pct": 0, "projecao_wh": 6516.000, "alerta_potencia": false },
  "anomalias": [],
  "tensao_faixa": "adequada",
  "saltos": 0,
  "pzem": { "ok": 601, "falha": 0 }
}
```

| Campo | Significado |
|---|---|
| `device` | `et-` + MAC do Wi-Fi: identifica a placa |
| `seq` | sobe 1 a cada ciclo de envio, com ou sem sucesso; um buraco = envio perdido |
| `uptime_s` | segundos desde o boot. A placa não tem relógio de parede: o backend carimba a hora ao receber |
| `leitura` | V, A, W, Hz, fator de potência e o contador de energia do PZEM; `null` enquanto o PZEM não respondeu nenhuma vez |
| `energia` | dia (1, 2...), energia do dia, do dia anterior e desde o boot (Wh), potência média do dia (W), lacunas recuperadas |
| `meta` | `estado` (`OK`, `PROJECAO_ACIMA`, `ATENCAO`, `ESTOURADA`), uso em %, projeção do dia (Wh), alerta de potência |
| `anomalias` | nomes dos alertas ativos: `SUBTENSAO`, `SOBRETENSAO`, `FREQUENCIA`, `FP_BAIXO`, `SALTO_POTENCIA`, `SENSOR_SEM_RESPOSTA` |
| `tensao_faixa` | `adequada`, `precaria` ou `critica` (PRODIST) |
| `saltos`, `pzem` | saltos de potência desde o boot; leituras do PZEM boas e com falha |

O contrato completo está na especificação da feature telemetria-wifi; quem
for escrever o backend de verdade pode usar `problemasDoContrato()` de
`tools/backend-teste.mjs` como referência.

### A linha `rede:` do relatório

```text
rede: wifi=conectado ip=192.168.0.23 | envios ok=118 falha=2 (wifi=1 rede=0 http=1 json=0) | ultimo=HTTP 503 | seq=120 | envio max=85ms
```

`wifi` = sem IP na hora do envio; `rede` = sem resposta (backend fora do ar,
IP errado, firewall); `http` = o backend respondeu com erro (o código aparece
em `ultimo`); `json` = o corpo não coube no buffer (não deveria acontecer).

## Ajustes

Metas e limiares ficam em `include/energy_config.h`: tensão nominal
(`CFG_NOMINAL_VOLTAGE` 220 ou 127, faixas do PRODIST), meta diária (5 kWh),
limite de potência (3000 W por 10 s), frequência e fator de potência.
Prioridades, períodos e deadlines ficam em `src/rt/rt_tasks.c` (inclusive o
intervalo de envio da `net`, em `RT_NET_TASK`) — mudou lá, atualize
`docs/diagrama-tarefas.md` (o teste AC-021 confere). Pinos, só em
`include/board_pins.h` (o teste AC-019 confere as regras do ESP32).
