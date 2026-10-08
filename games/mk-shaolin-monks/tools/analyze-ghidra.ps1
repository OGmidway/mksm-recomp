param([switch]$Import)
$ErrorActionPreference='Stop'
$gameRoot = Split-Path -Parent $PSScriptRoot
$recompRoot=[IO.Path]::GetFullPath((Join-Path $gameRoot '../..'))
$ghidra=Join-Path $recompRoot 'Ghidra'
$elf=Join-Path $ghidra 'mksm_elf/SLUS_210.87'
if((Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash -ne 'B1ED99D81B07969553ED2F9AFAAD4CAED1BBEB637D53A369F4DAFCA8EEDD2AB2') {throw 'Unexpected ELF identity'}
$projectDir=Join-Path $gameRoot 'ghidra/projects'
$exportDir=Join-Path $gameRoot 'ghidra'
$scriptDirs=(Join-Path $exportDir 'scripts')+';'+(Join-Path $recompRoot 'ps2xRecomp/tools/ghidra')
$mode=@('-process','SLUS_210.87')
if($Import -or -not (Test-Path (Join-Path $projectDir 'MKSM_NTSC.gpr'))) {$mode=@('-import',$elf)}
& (Join-Path $ghidra 'support/analyzeHeadless.bat') $projectDir MKSM_NTSC @mode -processor 'r5900:LE:32:default' -cspec default -scriptPath ('"'+$scriptDirs+'"') -preScript ConfigureMKSM.java $elf -postScript ExportPS2Functions.java (Join-Path $exportDir 'ghidra.toml') (Join-Path $exportDir 'functions.csv') -analysisTimeoutPerFile 600 -max-cpu 4 -log (Join-Path $gameRoot 'logs/ghidra-analysis.log') -scriptlog (Join-Path $gameRoot 'logs/ghidra-scripts.log')
if($LASTEXITCODE -ne 0) {throw "Ghidra failed: $LASTEXITCODE"}
