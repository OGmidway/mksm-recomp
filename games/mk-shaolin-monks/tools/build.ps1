param([switch]$Analyze, [switch]$Generate)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$driver = Join-Path $root 'tools/project.py'
if ($Analyze -or $Generate) {
    & python $driver tools
    if ($LASTEXITCODE -ne 0) { throw 'Tool build failed' }
}
if ($Generate) {
    & python $driver generate
    if ($LASTEXITCODE -ne 0) { throw 'Generation failed' }
}
& python $driver runtime
if ($LASTEXITCODE -ne 0) { throw 'Runtime build failed' }
