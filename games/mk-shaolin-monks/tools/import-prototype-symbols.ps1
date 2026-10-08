param()
$ErrorActionPreference='Stop'
$gameRoot = Split-Path -Parent $PSScriptRoot
$root=[IO.Path]::GetFullPath((Join-Path $gameRoot '../..'))
& python (Join-Path $gameRoot 'tests/transfer-prototype-symbols.py')
if($LASTEXITCODE -ne 0){throw 'Symbol validation failed'}
$scriptDirs=(Join-Path $gameRoot 'ghidra/scripts')+';'+(Join-Path $root 'ps2xRecomp/tools/ghidra')
foreach($build in @('retail','prototype')) {
 $project='MKSM_NTSC';$csv='functions.csv';$toml='ghidra.toml'
 if($build -eq 'prototype'){$project+='/prototype';$csv='prototype-functions.csv';$toml='prototype.toml'}
 $log=Join-Path $gameRoot ('logs/symbol-import-'+$build+'.log')
 & (Join-Path $root 'Ghidra/support/analyzeHeadless.bat') (Join-Path $gameRoot 'ghidra/projects') $project -process SLUS_210.87 -noanalysis -scriptPath ('"'+$scriptDirs+'"') -postScript ImportMKSMVerifiedSymbols.java (Join-Path $gameRoot 'ghidra/verified-symbols.tsv') $build -postScript ExportPS2Functions.java (Join-Path $gameRoot ('ghidra/'+$toml)) (Join-Path $gameRoot ('ghidra/'+$csv)) *> $log
 if($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'MKSM_SYMBOLS_IMPORTED=39' -Quiet)){throw "Symbol import failed; see $log"}
}
Write-Output 'Imported 24 functions and 15 globals into both verified Ghidra programs.'
