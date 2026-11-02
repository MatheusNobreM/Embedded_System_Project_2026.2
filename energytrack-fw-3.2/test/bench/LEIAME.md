# Prova de bancada (critério de aceite AC-018)

Esta pasta recebe o log serial da Pico W rodando o `energytrack_fw`. O teste
`test/spec/entregaveis.spec.test.mjs` lê `serial-bancada.log` e só aprova se o
último relatório mostrar:

- pelo menos 600 s de execução (`rt t=600s` ou mais);
- `miss=0` nas tarefas `proc`, `adc` e `pzem`, com `rmax` abaixo do deadline `d`;
- pelo menos 95% de leituras válidas do PZEM (`PZEM ok=... falha=...`);
- a linha `>>> RT PASS <<<`.

## Como capturar

1. Grave `build/energytrack_fw.uf2` (segure BOOTSEL ao ligar o USB e arraste o arquivo).
2. Ligue o PZEM-004T em GP0/GP1 pelo conversor de nível, com uma carga ligada.
3. Feche qualquer monitor serial aberto na porta da Pico.
4. Na pasta `energytrack-fw-3.2`, rode:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1
   ```

   O script acha a porta sozinho (ou use `-Porta COM5`), grava por 11 min e para.
5. Na raiz do repositório, rode o verify do onp-spec para registrar a prova.

Se o log mostrar `miss` maior que zero, não apague nem edite o arquivo: ele é a
evidência. Investigue a tarefa apontada e capture de novo.
