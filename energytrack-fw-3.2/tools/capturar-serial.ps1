<#
.SYNOPSIS
  Captura o log serial (USB-CDC) da Pico W para a prova de bancada do WPS 3.1.

.DESCRIPTION
  Abre a porta COM da Pico W (detectada pelo VID 2E8A da Raspberry Pi quando
  -Porta nao e informada), grava cada linha em test\bench\serial-bancada.log e
  mostra na tela. Para sozinho depois de -Minutos (padrao 11: os 10 min do
  aceite + 1 de folga). O teste do criterio de aceite AC-018 le esse arquivo.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\capturar-serial.ps1 -Porta COM5 -Minutos 12
#>
param(
    [string]$Porta,
    [int]$Minutos = 11,
    [string]$Saida = (Join-Path $PSScriptRoot '..\test\bench\serial-bancada.log')
)

$ErrorActionPreference = 'Stop'

if (-not $Porta) {
    $pico = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.PNPDeviceID -match 'VID_2E8A' -and $_.Name -match '\(COM\d+\)' } |
        Select-Object -First 1
    if (-not $pico) {
        throw 'Pico W nao encontrada na USB. Grave o energytrack_fw.uf2, ligue o cabo ou passe -Porta COMx.'
    }
    $null = $pico.Name -match '\((COM\d+)\)'
    $Porta = $Matches[1]
}

$Saida = [System.IO.Path]::GetFullPath($Saida)
New-Item -ItemType Directory -Force -Path (Split-Path $Saida) | Out-Null

$sp = New-Object System.IO.Ports.SerialPort $Porta, 115200, 'None', 8, 'One'
$sp.DtrEnable = $true     # o stdio USB da Pico so envia com DTR ligado
$sp.RtsEnable = $true
$sp.ReadTimeout = 1000
$sp.NewLine = "`n"
$sp.Open()

$fim = (Get-Date).AddMinutes($Minutos)
$writer = New-Object System.IO.StreamWriter($Saida, $false, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Capturando $Porta por $Minutos min em $Saida (Ctrl+C para parar antes)"
try {
    while ((Get-Date) -lt $fim) {
        try {
            $linha = $sp.ReadLine().TrimEnd("`r")
        } catch {
            $e = $_.Exception
            if ($e -is [System.TimeoutException] -or $e.InnerException -is [System.TimeoutException]) { continue }
            throw
        }
        $writer.WriteLine($linha)
        $writer.Flush()
        Write-Host $linha
    }
} finally {
    $writer.Close()
    $sp.Close()
}

Write-Host ''
Write-Host 'Captura concluida. Na raiz do repositorio, rode:'
Write-Host '  node .claude/skills/onp-spec-driven/scripts/onp-spec.mjs verify aquisicao-processamento'
