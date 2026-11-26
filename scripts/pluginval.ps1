# Validates one Composer.vst3 with pluginval (strictness 10) on Windows; the workflows call it.
# Usage: scripts/pluginval.ps1 build/debug/Composer_artefacts/Debug/VST3/Composer.vst3
param([Parameter(Mandatory)][string] $Plugin)
$ErrorActionPreference = 'Stop'

$pin = @{}
Get-Content "$PSScriptRoot/pluginval.version" | Where-Object { $_ -match '^(\w+)=(.+)$' } | ForEach-Object { $pin[$Matches[1]] = $Matches[2] }

Invoke-WebRequest "https://github.com/Tracktion/pluginval/releases/download/$($pin.version)/pluginval_Windows.zip" -OutFile pluginval.zip
$hash = (Get-FileHash pluginval.zip -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $pin.sha256_Windows) { throw "pluginval.zip has SHA-256 $hash, expected $($pin.sha256_Windows)" }
Expand-Archive pluginval.zip -DestinationPath pluginval -Force

# Validation never reports usage, whatever this user answered in the editor.
$env:DO_NOT_TRACK = '1'
# A GUI-subsystem exe: wait for it explicitly, or PowerShell moves on at once.
$p = Start-Process pluginval/pluginval.exe -Wait -NoNewWindow -PassThru -ArgumentList `
    '--strictness-level', '10', '--timeout-ms', '120000', '--validate', $Plugin
exit $p.ExitCode
