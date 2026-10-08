param([int]$TimeoutSeconds = 45)
$ErrorActionPreference = 'Stop'
$gameRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path Env:PS2_CONTROLLER_CONFIG)) { $env:PS2_CONTROLLER_CONFIG=Join-Path $gameRoot 'config/controllers.ini' }
if ($TimeoutSeconds -lt 1 -or $TimeoutSeconds -gt 300) { throw 'TimeoutSeconds must be 1 through 300' }
if (-not (Test-Path Env:PS2_AUDIO_MUTE)) { $env:PS2_AUDIO_MUTE='1' }
$root = [IO.Path]::GetFullPath((Join-Path $gameRoot '../..'))
$exe = Join-Path $root 'out/mk-runtime/ps2xRuntime/Release/ps2EntryRunner.exe'
$elf = Join-Path $root 'MortalKombatShaolinMonks/SLUS_210.87'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the runner first using tools/build.ps1' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stdout = Join-Path $gameRoot "logs/boot-$stamp.stdout.log"
$stderr = Join-Path $gameRoot "logs/boot-$stamp.stderr.log"
$process = Start-Process -FilePath $exe -ArgumentList ('"' + $elf + '"') -WorkingDirectory (Join-Path $gameRoot 'runtime') -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$completed = $process.WaitForExit($TimeoutSeconds * 1000)
if (-not $completed) { Stop-Process -Id $process.Id; $process.WaitForExit() }
$result = [pscustomobject]@{Completed=$completed;TimedOut=(-not $completed);ExitCode=$process.ExitCode;Stdout=$stdout;Stderr=$stderr}
$result | ConvertTo-Json | Set-Content (Join-Path $gameRoot "logs/boot-$stamp.result.json")
# Keep only the three newest complete boot capture groups in this directory.
$logRoot = [IO.Path]::GetFullPath((Join-Path $gameRoot 'logs'))
$expired = Get-ChildItem -LiteralPath $logRoot -File -Filter 'boot-*.result.json' |
    Where-Object { $_.Name -match '^boot-\d{8}-\d{6}\.result\.json$' } |
    Sort-Object Name -Descending | Select-Object -Skip 3
foreach ($capture in $expired) {
    $prefix = $capture.Name -replace '\.result\.json$', ''
    foreach ($suffix in @('.result.json', '.stdout.log', '.stderr.log')) {
        $target = [IO.Path]::GetFullPath((Join-Path $logRoot ($prefix + $suffix)))
        if ([IO.Path]::GetDirectoryName($target) -ne $logRoot) { throw 'Invalid capture path' }
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
}
$result | Format-List
Get-Content -LiteralPath $stdout -Tail 40
Get-Content -LiteralPath $stderr -Tail 40
