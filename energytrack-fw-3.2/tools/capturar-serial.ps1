<#
.SYNOPSIS
  Captura o log serial do ESP32 DevKit para a prova de bancada do WPS 3.1.

.DESCRIPTION
  Abre a porta COM da ponte USB-serial do DevKit (detectada pelo VID da
  Silicon Labs CP210x, 10C4, ou da WCH CH340/CH9102, 1A86, quando -Porta nao
  e informada), reinicia a placa pelo RTS para o log comecar no boot, grava
  cada linha em test\bench\serial-bancada.log e mostra na tela. Para sozinho
  depois de -Minutos (padrao 11: os 10 min do aceite + 1 de folga). O teste do
  criterio de aceite AC-018 le esse arquivo.

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
    $ponte = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.PNPDeviceID -match 'VID_(10C4|1A86)' -and $_.Name -match '\(COM\d+\)' } |
        Select-Object -First 1
    if (-not $ponte) {
        throw 'ESP32 DevKit nao encontrado na USB (CP210x/CH340). Ligue o cabo, instale o driver da ponte ou passe -Porta COMx.'
    }
    $null = $ponte.Name -match '\((COM\d+)\)'
    $Porta = $Matches[1]
}

$Saida = [System.IO.Path]::GetFullPath($Saida)
New-Item -ItemType Directory -Force -Path (Split-Path $Saida) | Out-Null

$sp = New-Object System.IO.Ports.SerialPort $Porta, 115200, 'None', 8, 'One'
# No DevKit, DTR e RTS da ponte comandam GPIO0 (boot) e EN (reset). DTR fica
# desligado (GPIO0 solto = boot normal) e um pulso no RTS reinicia a placa.
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.ReadTimeout = 1000
$sp.NewLine = "`n"
$sp.Open()
$sp.RtsEnable = $true
Start-Sleep -Milliseconds 100
$sp.RtsEnable = $false

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
