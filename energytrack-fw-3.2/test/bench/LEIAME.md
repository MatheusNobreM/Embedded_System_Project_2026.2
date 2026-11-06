# Prova de bancada (critério de aceite AC-018)

Esta pasta recebe o log serial do ESP32 DevKit rodando o `energytrack_fw`. O
teste `test/spec/entregaveis.spec.test.mjs` lê `serial-bancada.log` e só
aprova se o último relatório mostrar:

- pelo menos 600 s de execução (`rt t=600s` ou mais);
- `miss=0` nas tarefas `proc`, `adc` e `pzem`, com `rmax` abaixo do deadline `d`;
- pelo menos 95% de leituras válidas do PZEM (`PZEM ok=... falha=...`);
- a linha `>>> RT PASS <<<`.

## Como capturar

1. Na pasta `energytrack-fw-3.2`, grave o firmware:
   `pio run -e energytrack_fw -t upload` (ou **Upload** no PlatformIO, ambiente
   `energytrack_fw`). Se a gravação não começar, segure o botão BOOT ao conectar.
2. Ligue o PZEM-004T na UART2 (GPIO17 TX → RX do PZEM, GPIO16 RX ← TX do
   PZEM) pelo conversor de nível, com uma carga ligada, e o LM35 no GPIO34.
3. Feche qualquer monitor serial aberto na porta do DevKit.
4. Ainda em `energytrack-fw-3.2`, rode:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1
   ```

   O script acha a porta sozinho (ou use `-Porta COM5`), reinicia a placa, grava
   por 11 min e para.
5. Na raiz do repositório, rode o verify do onp-spec para registrar a prova.

Se o log mostrar `miss` maior que zero, não apague nem edite o arquivo: ele é a
evidência. Investigue a tarefa apontada e capture de novo.
