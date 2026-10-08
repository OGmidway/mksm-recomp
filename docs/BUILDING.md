# Building MKSM Recomp

## Requirements

- Git, Python 3.11+, CMake 3.21+ and a C++20 compiler.
- Windows: Visual Studio 2022 C++ build tools and Windows SDK (x64). This is the tested development platform.
- Linux/macOS: a suitable C++20 toolchain, graphics/audio development dependencies, pkg-config, and FFmpeg development libraries (`avcodec`, `avformat`, `avutil`, `swresample`, `swscale`). These hosts need contributor validation; no working-port claim is made.
- Internet access for CMake's pinned dependency downloads. Windows FFmpeg is fetched by CMake; movie support stays enabled.
- Optional: Ghidra with the Emotion Engine extension for new analysis, and Pillow for image-analysis probes. Basic generation does not require Ghidra or the prototype.

## Supply your own retail files

Extract your own NTSC-U retail disc while preserving its file names and directory structure:

```text
MortalKombatShaolinMonks/
  SLUS_210.87
  GAMEDATA.WAD
  IOP/
  ...remaining disc files, including movies...
```

The required executable SHA-256 is `b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2`. Other revisions are not supported by these address maps. Game paths are ignored by Git. Do not commit an ISO or extracted content.

```sh
python tools/project.py check
python tools/project.py tools --jobs 2
python tools/project.py generate
python tools/project.py runtime --jobs 2
python tools/project.py run
```

`all` runs the three build/generation stages. `--elf /absolute/path/SLUS_210.87` can use an external extraction; runtime disc access defaults to the executable's directory. Use that argument consistently for generation, runtime configuration and launch. `--generator` can select an installed CMake generator. Start with two build jobs because generated retail C++ is large.

Generation produces main translations, 847 supplemental map rows, and the selected PR244 entry subset from retained boundaries. It also produces registration hooks. These outputs are local, ignored, and reproducible from the ELF. The current checked-in generator is used; the old PR244 binary is not required. The boundary subset was originally discovered while investigating upstream PR #244.

`out/tools` and `out/mk-runtime` are reused. Keep at most one additional focused-test build. Close the runner before rebuilding. Logs overwrite files under the game's ignored `logs/` directory. Interactive play is audible; Python diagnostic runs are muted unless `--audible` is supplied.

## Game-data-free runtime checks

A contributor can work on shared runtime tests without owning or uploading a game:

```sh
cmake -S . -B out/checks -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_FFMPEG=OFF -DPS2X_BUILD_MKSM_DEBUG_CHECK=ON
cmake --build out/checks --config Release --target ps2_vif_image_check ps2_vu_sub_check ps2_vu_sdk_check --parallel 2
```

IOP/RPC checks can also build independently, without graphics dependencies or game files:

```sh
cmake -S games/mk-shaolin-monks/tests -B out/iop-checks
cmake --build out/iop-checks --config Release --parallel 2
```

Run `out/iop-checks/Release/mk_iop_checks.exe` on Visual Studio builds, or `out/iop-checks/mk_iop_checks` on single-config Unix builds. Use this as the third build instead of `out/checks` when working on IOP.

Run those graphics executables under `out/checks/ps2xRuntime/Release/` on Visual Studio builds, or `out/checks/ps2xRuntime/` with single-config generators. These checks deliberately disable movie decoding; the game build enables it. Retail-function checks appear only after the required local generated sources exist.

## Reproduce the current scene investigation

After building with local retail files:

```sh
python games/mk-shaolin-monks/tools/debug-runtime.py --seconds 155 --skip-intro --press-start-at 40 --press-cross-at 12 --press-cross-at 16 --press-cross-at 20 --press-cross-at 46 --press-cross-at 58 --press-cross-at 71 --press-cross-at 84 --capture-feedback --pixel-delay-ms 120000
```

This is an instrumented shortcut for scene investigation, not proof of full intro progression. Input timings depend on host speed. Confirm the visible character/difficulty selections and inspect fresh session/PID data. The expected complete path is Liu Kang, Normal, opening.sfd, Loading, Goro's Lair title, falling characters, Raiden dialogue and gameplay.

Some historical probe scripts use `logs/first-stage-relinked` instead of `logs/inspector`; pass `--run-dir` to the new replay script or use the documented harness override when continuing those experiments. Captures contain game memory: keep them local and share compact findings plus reproduction commands.

## Validation limits

The original local runtime has substantial test/live evidence. A clean-clone generation check is recorded in [STATUS.md](STATUS.md). A newly generated build is not automatically equivalent to every historical manually refreshed build. Report differences and include source commit, compiler, ELF hash and fresh inspector session. Never claim gameplay from a passing synthetic test or a HUD alone.

## Optional GitHub automation

`tools/ci/source-checks.yml` contains the source audit and focused Windows runtime checks. A maintainer with workflow permission can copy it to `.github/workflows/source-checks.yml`. Automation is not enabled in this initial publication; all commands are also available above for local use.

The lightweight IOP audio lookup tables are reproducible without game files using `python tools/generate-audio-tables.py`.
