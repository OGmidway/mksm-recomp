$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
& python (Join-Path $root 'tools/project.py') generate --only main @args
if ($LASTEXITCODE -ne 0) { throw 'Generation failed' }
