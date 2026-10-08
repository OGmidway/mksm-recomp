$ErrorActionPreference='Stop'
$gameRoot = Split-Path -Parent $PSScriptRoot
$recompRoot=[IO.Path]::GetFullPath((Join-Path $gameRoot '../..'))
$project=Join-Path $gameRoot 'ghidra/projects/MKSM_NTSC.gpr'
if(-not (Test-Path -LiteralPath $project)) {throw 'Run tools/analyze-ghidra.ps1 first'}
Start-Process -FilePath (Join-Path $recompRoot 'Ghidra/ghidraRun.bat') -ArgumentList ('"'+$project+'"') -WorkingDirectory $recompRoot -WindowStyle Hidden
