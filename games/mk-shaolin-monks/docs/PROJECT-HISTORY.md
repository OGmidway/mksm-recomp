# Mortal Kombat: Shaolin Monks PC Recomp

## Plain-language project history

This document explains what we started with, what we changed, what works now, and what still needs work.

The goal is a native PC version of the retail NTSC PlayStation 2 release of **Mortal Kombat: Shaolin Monks**. “Native” means rebuilding the original PS2 code so it can run on a PC instead of running the game through a PS2 emulator.

The project is not a finished PC port yet. It can now boot the retail game, show a stable front end, accept controller input, play the opening movie, and reach the loading screen. The next major task is getting from that loading screen into the first playable level.

## Where we started

We began with the retail PS2 ISO, an extracted game, the PS2 recomp tools, Ghidra, and the retail executable `SLUS_210.87`.

We also used:

- `MKSMprototype` for symbols and debugging clues.
- The Xbox version as a comparison reference for names, data formats, and behavior.
- The upstream PS2Recomp work, including PR #244, for general recomp ideas.

The retail PS2 build remains the actual game being rebuilt. The prototype and Xbox files are references only.

The organized project is located at:

```text
<repo>\games\mk-shaolin-monks
```

## Simple glossary

- **Recomp**: rebuilding the original PS2 machine code into portable C++.
- **ELF**: the PS2 executable file. Ours is `SLUS_210.87`.
- **Ghidra**: the program used to inspect the original executable and understand its functions.
- **IOP**: the PS2 input/output processor. It handled pads, memory cards, sound, and disc access.
- **RPC**: messages sent between the main PS2 processor and IOP services.
- **SFD**: the game's movie format. `OPENING.SFD` is the opening cinematic.
- **ADPCM**: a compressed sound format used by many game sound effects.
- **Runtime Debugger**: the debugger window inside the recomp runner. Press **F1**.
- **Inspector**: a Python tool that watches the running game and records useful state.

## What we have done

### Project organization and workflow

The game now has separate folders for configuration, documentation, tools, tests, logs, game data, and build output. The workflow is:

1. Reproduce a problem.
2. Capture evidence from the runtime debugger and inspector.
3. Check the original retail code in Ghidra.
4. Use Python for a quick experiment when useful.
5. Implement the confirmed behavior in portable C++.
6. Rebuild and test again.

This keeps fixes based on evidence instead of guesses.

### Startup and archive loading

The early runtime had a scheduler/stack problem that could corrupt the game's heap. That corruption caused archive reads and later loading to fail. The scheduler behavior was corrected.

We also fixed 64-bit handling of packed WAD archive offsets and sizes. Archive boot now transfers the expected data and verifies it with a known checksum.

### Runtime debugger and Python inspector

The runtime debugger can show CPU registers, memory, IOP services, RPC traffic, controller state, graphics state, DMA activity, and logs. Press **F1** while the game is running.

The Python inspector can watch the current game phase, character and difficulty choices, movie playback, audio timing, IOP/RPC activity, controller input, loading progress, and indirect calls. It keeps diagnostic history capped so the workspace does not fill up.

### Display and menu shaking

The movie was originally positioned incorrectly and was clipped at the bottom of the window. The display setup was restored so the movie is placed correctly.

The menus and memory-card warning were also shaking up and down. The problem was caused by incorrect handling of PS2 interlaced video fields. The presentation code now combines the fields correctly, keeping the menus stable.

The front-end sequence now reaches:

```text
Character Select
    -> character confirmation/name path
    -> difficulty menu
    -> Opening movie
```

### Controller support

PCXS2-style Pad1 mappings were added for:

- Xbox controllers.
- DualSense controllers.
- DualShock 4 controllers.

A physical DualSense reached the title/front-end path. The profiles are installed, but every controller and every button still needs a full live verification.

We also fixed a PS2 pad DMA issue. The pad data existed, but the retail wrapper was reading the wrong memory area. Copying the data into the expected bank corrected the false button presses and held-input behavior.

### IOP and RPC services

The trace with service ID `0x53465356` and RPC `0x8000` was identified as the game's SFSV sound-driver endpoint.

The runtime now has the startup sound transport, idle behavior, and back-pressure handling. CRI/ADX/SJU/RNA transport was also implemented for movie audio, including queues, PCM conversion, buffer ownership, and basic volume, pan, and rate commands.

This does not mean every IOP/RPC call is finished. The important startup, front-end, and movie calls are covered, while more calls will appear as the level loader is reached.

### Sound effects and audio

The direct `sndfi.irx` sound path now supports sound-bank registration and unloading, ADPCM sample upload and decoding, pitch, pan, gain, voice lifetime, looping, and a host PCM output path.

The ADPCM behavior was checked in Python first and then implemented in C++. A live inspector run showed completed voices and no stuck sound batches in the front end.

The high-pitched screech reported during earlier tests has not been declared fixed yet because the latest diagnostic runs were muted. It still needs an unmuted live check.

### Opening movie stall

The opening movie used to turn black after a few pictures. The video decoder was still running, but the game's presentation code was waiting for an audio clock that stayed at zero.

The deeper problem was movie audio cleanup. The old RNA teardown discarded pending audio buffers instead of returning them to their owner. The next part of the game then waited forever.

The teardown path now returns pending buffers safely, closes the stream, and removes the voice. The focused regression test fails with the old behavior and passes with the new behavior.

### Opening movie handoff

The real movie file is:

```text
FRONT\MOVIES\OPENING.SFD
```

A no-skip run reached the opening movie naturally, served thousands of movie pictures, advanced the audio clock, and finished at the game's loading screen. This proves the movie decoder and movie-audio handoff are no longer the main blocker.

## What works now

| Area | Result |
| --- | --- |
| Build and launch | The native runner starts the retail ELF. |
| Runtime debugger | Available with **F1**. |
| Python inspector | Self-test passes and captures runtime reports. |
| Archive boot | WAD transfer and checksum verification pass. |
| Front-end display | Menus and movies are visible in the correct area. |
| Menu stability | The constant vertical shaking was corrected. |
| Character flow | Character selection and difficulty selection are reached. |
| Controllers | Xbox, DualSense, and DualShock profiles are installed. |
| Front-end sound | Sound banks and voices can start and finish. |
| Movie video | FFmpeg-backed SFD/IPU decoding plays the opening movie. |
| Movie audio | CRI/RNA transport and shutdown ownership work well enough to finish the movie. |
| Focused tests | IOP tests, native fixtures, archive boot, and inspector self-tests pass at the current checkpoint. |

## What still needs work

### First playable level

The game currently reaches the loading screen after the opening movie, but the level loader still needs more retail state setup and indirect-call targets. These are being traced in the runtime debugger, checked in Ghidra, tested in Python, and then implemented in C++.

### 3D gameplay

The front end and movie are visible, but full level rendering, characters, enemies, collision, camera movement, effects, combat, and gameplay still need to be verified after the level loader works.

### Audio quality

The audio path is functional enough to unblock startup and movies. The high-pitched screech still needs an unmuted test so we can identify whether it is caused by sample rate, buffer format, or duplicate voices.

### Controllers

Analog sticks, triggers, D-pad, Start/Options, Select/Share, vibration, and exact Cross/Circle labeling still need full checks on each controller family.

### More IOP/RPC calls

The game uses more IOP/RPC services than the startup path alone. Each new unhandled service needs to be captured, identified, and implemented according to the retail behavior.

## How to test

```powershell
Set-Location <repo>\games\mk-shaolin-monks
.\tools\build.ps1
.\run.cmd
```

The normal launch is muted while debugging. Close the game before rebuilding so files are not locked.

Default keyboard input:

- **Enter** = Start.
- **Arrow keys** or **W/A/S/D** = D-pad.
- **Z/X/C/V** = Square/Cross/Circle/Triangle.
- **Space** = Cross.

Press **F1** for the runtime debugger.

Quick inspector checks:

```powershell
python tools/debug-runtime.py --self-test
python tools/debug-runtime.py --seconds 100 --trace-frontend
```

## Folder map

```text
<repo>\
├─ games\mk-shaolin-monks\   <- this game project
│  ├─ config\                 <- retail function/config data
│  ├─ docs\                   <- guides and reversing notes
│  ├─ game\                   <- extracted game data and runtime assets
│  ├─ logs\                   <- capped diagnostics and test output
│  ├─ out\                    <- build output
│  ├─ tests\                  <- focused regression tests
│  └─ tools\                  <- build, run, and Python tools
├─ Ghidra\                    <- Ghidra projects and analysis setup
└─ MKSMprototype\             <- prototype reference material
```

More detailed notes are in `docs/WORKFLOW.md`, `docs/INSPECTOR.md`, `docs/SFSV-RPC.md`, `docs/PROTOTYPE.md`, `docs/CONTROLLERS.md`, and `docs/PR244-review.md`.

## Next goal

The immediate target is the transition from the loading screen into the first level. After that, the same evidence-first process will be used for 3D rendering, camera movement, collision, combat, and the remaining gameplay systems.

## Current status

The native recomp can boot the retail game, show a stable front end, accept mapped controller input, play the opening SFD movie with its audio path, and reach the loading screen. The next major job is completing the retail level loader so actual gameplay can begin.
