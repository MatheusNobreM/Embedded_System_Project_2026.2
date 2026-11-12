# Provas de bancada (critérios de aceite AC-018 e AC-028)

Esta pasta recebe as evidências da placa rodando o `energytrack_fw`:

| Arquivo | Quem grava | Critério |
|---|---|---|
| `serial-bancada.log` | `tools/capturar-serial.ps1` (log serial do ESP32 DevKit) | AC-018 e AC-028 |
| `telemetria-recebida.jsonl` | `tools/backend-teste.mjs` (o que o backend de teste recebeu) | AC-028 |

## AC-018: 10 minutos sem estouro de deadline

O teste `test/spec/entregaveis.spec.test.mjs` lê `serial-bancada.log` e só
aprova se o último relatório mostrar:

- pelo menos 600 s de execução (`rt t=600s` ou mais);
- `miss=0` nas tarefas `proc` e `pzem`, com `rmax` abaixo do deadline `d`;
- pelo menos 95% de leituras válidas do PZEM (`PZEM ok=... falha=...`);
- a linha `>>> RT PASS <<<`.

## AC-028: os mesmos 10 minutos, enviando ao backend pelo Wi-Fi

O teste `test/spec/bancada-rede.spec.test.mjs` lê os dois arquivos e só
aprova se:

- o log serial passa em tudo do AC-018 **e** a última linha `rede:` mostra
  `wifi=conectado` com pelo menos um envio ok;
- o backend de teste recebeu, dentro do contrato do JSON, pelo menos 95% dos
  envios do último boot da placa (contados pela sequência `seq`), cobrindo
  pelo menos 600 s de `uptime_s`.

Uma captura com o Wi-Fi ligado prova os dois critérios de uma vez.

## Como capturar

1. Na pasta `energytrack-fw-3.2`, configure o Wi-Fi e a URL do backend
   (README, seção 6): `CONFIG_ET_WIFI_SSID`, `CONFIG_ET_WIFI_PASSWORD` e
   `CONFIG_ET_BACKEND_URL` em `sdkconfig.energytrack_fw`, ou pelo menuconfig.
   Para só o AC-018, pode deixar sem rede.
2. Grave o firmware: `pio run -e energytrack_fw -t upload` (ou **Upload** no
   PlatformIO, ambiente `energytrack_fw`). Se a gravação não começar, segure o
   botão BOOT ao conectar.
3. Ligue o PZEM-004T na UART2 (GPIO17 TX → RX do PZEM, GPIO16 RX ← TX do
   PZEM), com o PZEM ligado à rede e o transformador de corrente numa carga.
4. Em um terminal, ainda em `energytrack-fw-3.2`, suba o backend de teste
   (ele zera `telemetria-recebida.jsonl`):

   ```bash
   node tools/backend-teste.mjs
   ```

   A URL que ele imprime precisa ser a mesma do `CONFIG_ET_BACKEND_URL`.
5. Feche qualquer monitor serial aberto na porta do DevKit e, em outro
   terminal, rode:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1
   ```

   O script acha a porta sozinho (ou use `-Porta COM5`), reinicia a placa, grava
   por 11 min e para.
6. Pare o backend de teste (Ctrl+C) e, na raiz do repositório, rode o verify
   do onp-spec para registrar a prova.

Se o log mostrar `miss` maior que zero, ou o backend acusar envios faltando,
não apague nem edite os arquivos: eles são a evidência. Investigue a causa e
capture de novo.
