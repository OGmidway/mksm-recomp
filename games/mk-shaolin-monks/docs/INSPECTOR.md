# Runtime Inspector + Python

Paths in this document are relative to the MKSM workspace root unless stated otherwise. Current commands: [WORKFLOW.md](WORKFLOW.md).

Implemented and live-verified 2026-09-20. The bundled Runtime Debugger remains available with F1. The inspector reads the same scheduler and RPC history sources, and adds executor-boundary CPU/RAM snapshots for Python. It works regardless of debugger-panel visibility. It does not provide breakpoints, input injection or guest memory writes.

## Quick use

Run a bounded debugging session (opens the native game, captures for 20 seconds, then stops only its own runner):

```powershell

python <repo>/games/mk-shaolin-monks/tools/debug-runtime.py --seconds 20

```

Read the latest capture and show its age:

```powershell

python <repo>/games/mk-shaolin-monks/tools/debug-runtime.py --read

```

For an interactive session, launch `tools/run-inspector.ps1` and use the read command from another terminal. Close the game normally when finished. Use one runner at a time; the interactive launcher rejects a duplicate. The inspector is opt-in and does not embed Python in the native game.

Additional memory watches:

```powershell

python <repo>/games/mk-shaolin-monks/tools/debug-runtime.py --seconds 20 --memory-watch 'buffer=*0x755ac8:52'

```

The defaults watch SYSTEM_MEMORY at 0x64e998, the current 52-byte archive request through pointer 0x755ac8, libc break, verified debug flags, async_load_info and file_info. Recovered symbols identify matching PC/RA addresses; decoded state exposes allocator fields and loading phases. Both launchers verify the exact MKSM ELF SHA-256 before using these addresses. Custom watch names/addresses must be justified for this exact build.

For frontend investigation only, add `--skip-intro`. This explicitly sets `MKSM_DEBUG_SKIP_INTRO=1` for the owned runner. Its identity-gated hook scopes retail flag 0x513a04 to 1 while executing original intro-state routine 0x1d32e0, then restores the flag. The original branch returns state-machine result 3. The option is off by default and recorded in `report.json`; it does not fix or verify movie playback. The inspector itself remains read-only. The Python launcher removes inherited skip settings so ordinary runs retain the movie path.

## Data and retention

Fixed outputs are overwritten under `logs/inspector/`: inspector.json, report.json, stdout.log and stderr.log. JSON is replaced via a temporary sibling file, so readers do not observe a partially written document. Histories are capped at 60 CPU samples and 256 RPC entries. The final live snapshot was about 82 KB. Native stdout/stderr are session logs; the timed Python harness bounds sessions to 1–300 seconds. An interactive session can produce larger native logs if a runtime subsystem repeats diagnostics.

The version-1 schema includes PID/session, capture and write timestamps, guest PC/RA/SP/GP, low 64 bits of all GPRs, thread/wait state, runtime heap bounds, bounded physical RAM watches, IOP service metrics and RPC addresses/sizes/results with 16-byte payload previews. Optional `graphics` data exposes private display registers, framebuffer contexts, presentation dimensions, DMA/GIF counts and at most 24 GS history events (empty when GS history is paused). `pads` reports two ports' open/read/button state. These counters do not prove that a game scene was rendered or that gameplay works. It is not a full 128-bit register dump, complete RAM dump or per-instruction trace. This schema is project-local and does not assume compatibility with the external workflow viewer.

Sampling targets 250 ms intervals. CPU/RAM collection and file emission occur on the EE executor at safe dispatcher/event boundaries. Snapshot requests may be delayed by a long generated function; blocked state is sampled when the scheduler reaches its event-wait boundary. Subsystems are sampled separately, not simultaneously. The collector can add timing/I/O overhead, so disable it for performance measurements.

The first enabled inspector capture resumes the existing GS diagnostic history (512-event ring); each JSON snapshot exports only its latest 24 events. The Runtime Debugger can pause that history afterward. `ffmpeg_compiled` reports build capability. The optional `mpeg` object reports current player/decoder counts, queued/served pictures, players with input, sequence-header waits, feed calls and CD-stream bridge bytes. These are read under the MPEG mutex and reset with the relevant playback/stream state; they are not lifetime totals. A compiled decoder with zero feed calls is not evidence of successful movie playback.

Python rejects the wrong session/PID and captures older than three seconds during live runs. `--read` also works on saved captures, explicitly reporting their age/freshness; it does not prove that the original process remains alive. The live report records the ELF and runner hashes and counts RPC sequence gaps between polls. The retained ring reports how many earlier events have rolled out; overwriting history is not automatically a missed event if Python already observed it.

## Validation

- Python contract tests passed: schema, PID/session identity, stale/future timestamps and RPC reporting.

- Native fixture passed: actual compiled JSON writer, escaped session/watch names, last-valid-word RAM watch, overflowing range rejection, null pointer rejection and final stopped snapshot.

- Focused IRQ stack regression passed while producing the fixture.

- Final Release build passed. A 20-second live MKSM run delivered 68 fresh snapshots, five threads, 60 retained CPU samples and 256 retained RPC events. Python observed no RPC sequence gaps between polls in this run.

- Unhandled calls observed: SNDF 0x100/0x1300/0x1600; CRI 0x400/0x403/0x40a/0x40c/0x422. These are diagnostic observations, not implemented services.

- Captured later SYSTEM_MEMORY words include 0x80fdeb00 and 0x80f00e80, so later heap corruption remains. Last sample PC was 0x427838. This is not evidence of successful gameplay or rendering.

- Windows live testing completed; the C++ producer uses standard C++ with platform-specific atomic replacement/PID helpers. Linux/macOS builds and runtime behavior are untested.

Rerun checks:

```powershell

python <repo>/games/mk-shaolin-monks/tools/debug-runtime.py --self-test

python <repo>/games/mk-shaolin-monks/tests/verify-inspector.py

```

The native fixture target is `ps2_irq_stack_check` in the existing mk-runtime build, enabled by PS2X_BUILD_IRQ_STACK_CHECK. No new build tree was created.

## Prototype-assisted reversing update

The current symbol catalog is ghidra/retail-symbols.json (24 functions, 15 globals), with evidence and limitations in PROTOTYPE.md. The inspector decodes the recovered 104-byte allocation record, accepts legal PS2 pointer aliases and flags impossible byte lengths. It also follows the current archive request instead of always reading the first queue slot. The first observed bad heap sample is recorded in report.json; it is not the exact writing instruction.

The earlier heap-anomaly paragraph records a pre-fix run. Prototype-assisted analysis identified a 32-bit-versus-64-bit BGEZ translation error in retail GetAsyncFileSize. The corrected decoder passes native tests and advances loading without that anomaly in the tested 20-second runs. Full gameplay and the remaining IOP/RPC services are still unverified. Debug-menu/camera callbacks are preserved in the native supplement but not automatically activated.

Use --boot-trace for bounded allocator/archive diagnostics alongside snapshots. Reuse the same output files. Tests and final live state are summarized in docs/PROTOTYPE.md and logs/inspector/report.json.

## Game-frame and dispatch checkpoint

The timed harness enables `PS2_INSPECTOR_FRAME`. The render thread exports the presented game texture, cropped to display dimensions, every two seconds to the same `logs/inspector/frame.png`, with DMA/GIF/GS/VIF counters in `frame.png.json`. This excludes host debugger UI and desktop. Capture is opt-in and synchronous; disable it when measuring performance. Failure disables further frame exports for that run.

Before launching, Python removes the previous frame pair. After its runner stops, report.json records the frame hash, counters and owning session, plus at most eight missing-target diagnostics and two runtime-error lines. A frame may precede the final CPU snapshot; neither black pixels nor increased DMA counts establish gameplay. Frame/sidecar writes are not atomic, so inspect them after a bounded run finishes.

The September 20 receive-DMA fix passed a 30-second run past the former `sceDmaRecvN` exception: 114 fresh samples, no observed allocator anomaly or RPC sequence gaps; DMA submissions increased to 34. Subsequent callback recovery continues, with latest results overwriting report.json. The game texture was still entirely black at this checkpoint.

`tools/test-boot.ps1` retains three timestamped capture groups; 93 expired boot files were removed. Maintained build directories: out/tools, out/mk-runtime and out/mk-iop-checks. Obsolete PR build directories and source backup copies were removed; out/upstream-pr244 remains reference source.

## Silent audio investigation

Python diagnostic runs are muted by default following the reported screech. Host master output is zeroed after device initialization; callbacks, queue consumption and guest clocks remain active. `--audible` explicitly opts back into sound, which is still defective. Manual run.cmd and the PowerShell launchers default PS2_AUDIO_MUTE to 1 when unset.

`--capture-pcm` overwrites logs/inspector/pcm.bin with at most 2 MiB of successfully submitted stereo s16 samples. Each little-endian record contains PCMC magic, stream key, current sample rate, frame count, then samples. This is pre-device PCM, not a recording of the final speaker mix or underrun gaps. report.json binds the capture hash to its session and records mute confirmation from the runner. `python tests/probe-live-pcm.py` analyzes a normal intro capture and independently decodes midway.sfd with the installed FFmpeg, writing logs/live-pcm-probe.json. It does not play audio or alter guest state. The initial comparison confirms distortion, not a decoder fix.

## Fast presentation diagnosis

`tools/debug-runtime.py --seconds 17 --capture-primitives` overwrites at most 4096 draw records in `logs/inspector/primitives.csv`, beginning ten seconds after the first primitive. It records framebuffer, XYOFFSET, texture and raw vertex coordinates; the report binds its hash to the runner session. `--capture-gif --gif-delay-ms 11000` delays the existing 512-packet / 8 MiB GIF capture until a menu is visible.

`--frame-interval-ms 33` overwrites the same native texture PNG rapidly. `tests/probe-screen-motion.py` uses NumPy/Pillow, retains 32 measurements and at most two fixed images; `--scene character` supplies the existing menu input sequence. Use the bundled Python with NumPy/Pillow for that probe. Confirm the saved image actually shows the intended scene before treating its motion measurements as evidence.

The menu-shake diagnosis is in `logs/menu-field-diagnosis.json`: raw draws showed 732/738 menu vertices alternating by half a framebuffer row. Retail `0x37e4e8` adds eight 12.4 coordinate units on one field; `ghidra/retail-menu-projection.txt` contains the original instructions. Python confirmed the two field images can form a stationary complete image, then the portable GS field-history implementation was verified with live captures. The comparison reference for GS FFMD field expansion/offset behavior is the [PCSX2 renderer](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/Common/GSRenderer.cpp); the runtime implementation is authored locally.

RPC request previews are now captured before dispatch, preserving input bytes when SNDF uses the same address for request and reply. The SFSV service's Runtime Debugger metrics include SNDF work/queue/status addresses and the first two header/wave partition bases. The text log prints each successful bank registration; debugger metrics include registered-bank/initial-tone counts and a tone-state fingerprint. logs/sndf-init-probe.json combines original-instruction fixtures with the latest validated live checkpoint; bank registration is verified; voice playback/completion remains pending.

## Character and difficulty trace

`--trace-frontend` adds six retail watches (all 16 slots including defaults) and records at most 48 state changes in report.json/frontend_samples. It decodes the dialog at 0x5ca0c8, its choice list through 0x5ca0d4, character-selection state at 0x5cc5f0, menu mode, saved profile difficulty, and sound-manager active count. Fields are read-only. A dialog kind of zero with heading ID 0x5a7 is the recovered difficulty setup; choice count/selection and saved profile difficulty are distinct observations. These fields do not prove text rendering, an audible announcement, or a movie transition. Invalid/truncated watches are omitted by the decoder. Python self-tests cover those cases. Live sessions 539051519bf8470e9fbe171fc4440413 and c030eed21b7e490a980531be019cc31e verified dialog creation/enabling and saved difficulty separately. The captured dialog visibly contains its title, choices and controls.

The SFSV service now reports voices prepared/playing/completed, PCM submitted/consumed frames, accepted/busy batches and current bank/tone ownership. These distinguish sound playback progress from a later movie stall. In session 0735648f13ed46a58f292326ce32944f, nine voices completed and sound-manager active_entries reached zero, followed by bank 2 unloading. A fresh decoder then stalled at pictures_served=3/feed_calls=2 with a black frame. This is not proof of opening.sfd playback; the movie feed/presentation path needs further investigation. Sound quality was not evaluated because the run was muted.

## Opening restart diagnostics

The CRI service now exposes bounded per-RNA metrics for its at most 16 objects: handle, playing state, channels/rate, submitted/consumed/returned frames, and both source queue handles/data-byte counts. Object destruction removes its row; aggregate counters are not lifetime totals. Under `--boot-trace`, game hooks emit at most 60 movie-state records two seconds apart, including filename, codec clocks, input ring and frame descriptors. These are read-only diagnostics.

The opening black-frame stall was traced to lost pending PCM descriptors during RNA destruction and repaired. The 100-second verification visibly displayed the cinematic and advanced its audio clock; this supersedes the earlier three-picture stall. Keep complete playback, first-level rendering and audible quality as separate checks. For full-movie runs the inspector permits at most 360 seconds, retaining the existing 48-entry histories and overwriting the same outputs.

For the timing-gate probe, omit `--trace-frontend` and add these watches to the normal menu input schedule:

```powershell
--memory-watch 'movie_header=0x55e25c:256;movie_time=0xfae340:192;movie_config=0xfade00:160;adx=0x533a10:256;pcm=0x547abc:100;sjx=0x557be0:128'
```

These codec addresses were observed for opening.sfd in the current NTSC build. The probe verifies session/ELF identity and reads the codec pointer from movie_header; recapture correctly based ranges if allocation changes. `movie_time` covers codec+0xf80 and `movie_config` codec+0xa40. Do not interpret a stale watch from a destroyed movie as active playback.

The subsequent 345-second no-skip run completed the cinematic and captured the Loading screen. Its retained MPEG history records decoder removal at 299.617s. The current report exposes the next missing target 38be20 and later level-load loop 35f73c. Full gameplay remains unverified. The prior movie-clock capture is retained separately in cri-clock-probe.json; the latest inspector session uses frontend watches instead.

The graphics snapshot also reports `display_fbp`, `source_fbp`, and `used_preferred_source` from the existing presentation snapshot. These identify which GS buffer was requested and which was actually shown. A changed frame or texture atlas is not proof that the first level is rendering correctly.


## Bounded missing targets and vector-unit state

The default runtime missing-function diagnostic still reports one target per runtime. Set PS2_MISSING_TARGET_LIMIT to an integer from 1 through 16 to log that many distinct missing target addresses; values are clamped to that range and invalid text keeps the default. Python debug-runtime.py opts into 16. Repeat calls to the same target are suppressed, runtime instances have separate state, and diagnostic reset clears the list. Guest execution and BreakOnce policy are unchanged.

report.json retains at most 16 missing-target lines and includes missing_target_limit and missing_target_limit_reached. Reaching the cap means later missing functions may be unreported. mksm_debug_check --missing-report verifies bounded distinct logging, per-runtime isolation, reset, the default limit, and preserved guest state.

Each fresh inspector snapshot now includes vu: vpu_stat, arithmetic_status, vu1_pc, vu1_cycles, vu1_top, vu1_itop, vu1_ebit, vu1_halt_after_delay, vu1_stopped_d, and vu1_stopped_t. These read-only values are captured at the existing EE boundary; this runtime executes VU work synchronously there. Compare VPU_STAT bit 8 with actual CPU wait PCs when diagnosing a stall. Arithmetic STATUS is a separate register. The compiled snapshot-writer integration check validates JSON field types alongside existing freshness, memory-bound, and bounded-history checks.


## Physics allocator investigation

Additional --memory-watch entries now retain their last 48 changed samples in report.json.custom_watch_samples, including elapsed time, sequence, sampled PC, resolved address, validity and bytes. The sampled PC is not necessarily the writer. Histories are read-only and bounded; default/frontend histories are unchanged. Use the existing first-stage-relinked capture directory for stage tests.

For the current retail allocator, omit --trace-frontend and add:

```powershell
--memory-watch 'hk_memory_ptr=0x5b2010:4;hk_memory=*0x5b2010:96'
```

The indirect watch follows the live pointer instead of hardcoding its allocation address. A zero pointer yields an invalid object watch. MKSM_TRACE_HAVOK_COPY=1 enables an optional wrapper around original copy routine 0x20fde0. It logs at most 16 copies overlapping the first 96 bytes of the live allocator, or calls that change its +0x20 table pointer. It preserves the original function and does not repair or skip copies. This is focused diagnostic coverage, not a complete write watchpoint.


The vu object also includes vu0_vf0_bits (four unsigned 32-bit words, X/Y/Z/W) and vu0_mac_flags. Normal VF0 is [0,0,0,1065353216], representing (0,0,0,1). These fields come from the same saved EE context as the PC; they do not mutate the register. Use them to verify the shared VSUB correction during loading, together with the existing allocator watches.


## Scene packet investigation

Use `tests/probe-gif-stream.py --run-dir logs/first-stage-relinked` for boundaries and `tests/probe-scene-gif.py --run-dir logs/first-stage-relinked` for packed vertex, STQ, RGBA and fog samples. Both validate the capture session/hash, cap input at 512 records / 8 MiB, and overwrite their existing reports. The scene report keeps at most 96 ordinary vertices, 96 XYZF geometry samples and 96 register writes. Coordinate ranges still include XYOFFSET. Attribute samples are only reported when explicitly supplied within the same GIF tag. This is not a full GS replay: register-list draws, initial GS state and native rendering shortcuts are not reconstructed.

A delayed capture may contain an IMAGE header without inline bytes, followed by a new IMAGE tag containing its complete payload. The VIF path tracks these transfers with m_vif1PendingPath2ImageQwc. The boundary probe recognizes only that exact adjacent complete pair as a split transfer; isolated truncation and malformed packed payloads remain anomalies. The scene probe retains partial payloads explicitly rather than asserting that every captured call contains a complete upload. No guest or renderer state is modified.

The Python run report also counts vf0_samples_checked and retains only first_vf0_anomaly and first_negative_broadphase_bound. A null value means the failure was not observed at sampled boundaries, not exhaustive instruction coverage.

### Delayed geometry captures

`--capture-primitives --triangles-only --primitive-delay-ms 300000` records up to 4096 triangles after the delay, before raster clipping. CSV includes XYZ/STQ, color/fog, XYOFFSET, texture dimensions/format, scissor, TEST/ALPHA and depth/framebuffer write state. `python tests/probe-scene-triangles.py --run-dir logs/first-stage-relinked` verifies the session/hash and overwrites logs/scene-triangles-probe.json. Bounding-box overlap is not visible coverage or proof of correct geometry.

`--capture-vu1 --vu1-delay-ms 300000` records up to eight VU1 execute/resume inputs after the delay measured from the first execute. It overwrites vu1-starts.bin (at most 267520 bytes). Records use explicit little-endian words: thirteen version-2 header words, 155 state words, microcode and data. Header: magic 0x31555650, version 2, PC, TOP, ITOP, code bytes, data bytes, cycle budget, state-word count, D/T enable bits, elapsed milliseconds, resume flag (0/1). The reader also accepts version 1 without the resume word. State: 128 VF lane bits, 16 VI values, four ACC lanes, Q/P/I bits, then R/MAC/CLIP/STATUS. Each code/data region is capped at 16384 bytes. No host struct layout is serialized.

Run `python tests/probe-vu1-starts.py --self-test` for parser bounds/truncation checks, then `python tests/probe-vu1-starts.py --run-dir logs/first-stage-relinked` to verify the completed run hash/session and overwrite logs/vu1-starts-probe.json. Add `--vu1-distinct-entries` to retain only the first call at each PC. This captures program starts and resumes but not pipeline queues; it is not a complete replay snapshot. Nonfinite VF interpretations may be packed integer data. Both capture switches are off by default. The test duration ceiling is now 420 seconds to cover a delayed frontend plus complete opening playback and stage loading; history and capture caps are unchanged.

`python tests/probe-camera-matrices.py` compares the last 48 retained custom-watch snapshots against view/projection algebra. Use watches camera=0x6cbad0:48, view=0x660d60:64, projection=0x660de0:64 and viewproj=0x660f20:64. It overwrites logs/camera-matrices-probe.json. Asynchronous snapshots can catch partially updated camera state; algebraic agreement does not prove intended camera placement.

With `--capture-vu1`, the runtime also overwrites one xgkick-failure.bin if a packet fails. This is capped at 81,968 bytes (48-byte little-endian header, up to 64 KiB transferred packet, up to 16 KiB current VU data). Header: magic 0x314b4758, version 1, XGKICK issue PC, failure PC, source byte address, copied bytes, offending tag offset, requested tag bytes, capacity, VU data bytes, TOP, ITOP. `python tests/probe-xgkick-failure.py` validates the completed run session/hash and overwrites logs/xgkick-failure-probe.json. Text diagnostics are capped at eight per interpreter. The stop-on-failure behavior is preserved; no oversized packet is silently accepted.

## Focused VU replay and failure capture

`--capture-vu1 --vu1-on-xgkick-failure` retains the last eight pre-call VU inputs in memory (267,520 bytes maximum) and writes them only when the first XGKICK failure occurs. Use this without delay, distinct-entry, or PC filters. The normal eight-record capture remains available; `--vu1-entry-pc 0x...` optionally selects an aligned byte address below 0x4000.

Build the `ps2_vu_replay` target in the existing `out/mk-runtime` tree. Run `python tools/replay-vu.py --record 0 --cycles 1000000` from the game directory. Python checks the source session and capture hash, invokes the portable C++ interpreter, and overwrites `logs/vu-replay-report.json` plus the two bounded replay outputs in the selected run directory. Cycle budgets are limited to 1..1,000,000.

Replay starts with empty pipeline queues and fresh GS state. Resumed calls may depend on state absent from the capture. A reproduced fault is useful evidence; a successful replay does not prove live rendering is correct. This tool does not boot the game or validate gameplay.

`--vif-launch-pc 0x35e8` captures the first VIF1 command stream launching that byte PC into `vif-launch.bin`. The cap is 1 MiB plus a 56-byte header. Large streams retain the window ending immediately after the selected launch. The report binds the capture to the session and SHA-256. Header words: magic `0x31464956`, version 2, captured window size, launch-command byte offset, launch PC, then current CYCLE, MODE, MASK, BASE, OFFSET, TOPS and ITOPS, followed by the original window start and total stream size. These registers are measured at launch; they are not necessarily the stream's initial state. The payload is the raw command stream. The option defaults off.

`tests/probe-vif-launch.py` validates capture identity and walks command boundaries, retaining at most 48 command summaries and 32 unknown-command examples. A window that starts inside a stream is explicitly marked uncertain; use raw bytes and known boundaries before drawing conclusions.

With failure-triggered VU capture, a successful run with no XGKICK fault intentionally creates no vu1-starts.bin or xgkick-failure.bin. Use ordinary delayed/distinct captures for the next rendering investigation. The retained vif-launch.bin and logs/vif-launch-probe.json describe the pre-fix baseline; they must not be attributed to the latest post-fix session.

### Bounded analog movement probes

`debug-runtime.py --left-stick-at 125:10:255:128 --left-stick-at 150:10:0:128` applies right and left host-pad pulses, returning to neutral between them. Format: start seconds, duration seconds (maximum 30), LX, LY (0..255; neutral 128). Pulses cannot overlap or outlive the run. Input leases expire normally. Inspector pad records expose the axes actually read by the guest. The report retains at most 96 one-second samples near pulses, with requested axes, observed pad reads and custom memory watches. This proves input delivery; gameplay response needs separate evidence. Runner SHA is taken immediately after launch, before the process exits.

### Scene pixel provenance

`--capture-pixels --pixel-delay-ms 120000` records up to 1024 successful raster writes at GS coordinates (128,64) and (320,112). Frame-mode rows are doubled on display. `pixels.csv` includes pre-fog source color, old framebuffer value, final written value, texture/fog/blend/depth state. These are post-texture-combine inputs; raw texture texels are not recorded. Transfers and rejected fragments are not recorded. `tests/probe-scene-pixels.py` requires the latest completed report and matching SHA/session, then overwrites `logs/scene-pixels-probe.json`. Capture is opt-in and does not alter rendering.
