$ErrorActionPreference='Stop'
$gameRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path Env:PS2_CONTROLLER_CONFIG)) { $env:PS2_CONTROLLER_CONFIG=Join-Path $gameRoot 'config/controllers.ini' }
if (-not (Test-Path Env:PS2_AUDIO_MUTE)) { $env:PS2_AUDIO_MUTE='1' }
$recompRoot=[IO.Path]::GetFullPath((Join-Path $gameRoot '../..'))
$inspectDir=Join-Path $gameRoot 'logs/inspector'
New-Item -ItemType Directory -Force -Path $inspectDir | Out-Null
$env:PS2_INSPECTOR_FILE=Join-Path $inspectDir 'inspector.json'
$env:PS2_INSPECTOR_SESSION=[Guid]::NewGuid().ToString('N')
$env:PS2_INSPECTOR_WATCHES='system_heap=0x64e998:104;archive_request=*0x755ac8:52;libc_break=0x563fd4:4;gGodMode=0x4c7778:4;G_Free_Camera=0x511378:4;gObjectNum=0x50c134:4;menu_exit_requested=0x5d1e4c:4;gTest_no_particle_update=0x6bee10:4;async_load_info=0x4ef7e0:16;file_info=0x651e30:8'
$exe=Join-Path $recompRoot 'out/mk-runtime/ps2xRuntime/Release/ps2EntryRunner.exe'
$elf=Join-Path $recompRoot 'MortalKombatShaolinMonks/SLUS_210.87'
if((Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash -ne 'B1ED99D81B07969553ED2F9AFAAD4CAED1BBEB637D53A369F4DAFCA8EEDD2AB2') {throw 'Unexpected ELF identity'}
$existing=Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe}
if($existing) {throw 'This runner is already open. Close it before launching another inspector session.'}
Start-Process -FilePath $exe -ArgumentList ('"'+$elf+'"') -WorkingDirectory (Join-Path $gameRoot 'runtime') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $inspectDir 'stdout.log') -RedirectStandardError (Join-Path $inspectDir 'stderr.log')
