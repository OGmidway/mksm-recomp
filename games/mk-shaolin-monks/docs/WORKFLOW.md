# MKSM working workflow

Canonical workspace: **<repo>/games/mk-shaolin-monks**. Paths below are relative to that folder. Scripts resolve their own location and can also be invoked by absolute path. The active workflow is this file; research-guide/workflow is reference material from another game.

## Build and launch

```powershell
Set-Location <repo>/games/mk-shaolin-monks
.\tools\build.ps1
.\run.cmd
```

The build reuses <repo>/out/mk-runtime, enables FFmpeg and the Runtime Debugger, and stages hooks from runtime/ into runtime/output/. The executable is <repo>/out/mk-runtime/ps2xRuntime/Release/ps2EntryRunner.exe. Launchers use runtime/ as the working directory so memory cards and imgui.ini stay together. run.cmd defaults to audible audio; set PS2_AUDIO_MUTE=1 for a muted playtest. F1 toggles the Runtime Debugger. Enter is Start; arrows/WASD are D-pad; Z/X/C/V are Square/Cross/Circle/Triangle; Space also maps to Cross.

## Investigate with Python, then implement in C++

```powershell
python tools/debug-runtime.py --self-test
python tools/debug-runtime.py --seconds 6 --capture-gif --boot-trace
python tools/debug-runtime.py --read
```

The normal movie test retains the intro. For frontend investigation, use --skip-intro and deliberate input pulses, for example --press-cross-at 5 --press-cross-at 10 --press-cross-at 15 --press-start-at 21. Longer character-confirmation runs also use --press-cross-at 27 --press-cross-at 34 --press-cross-at 43 --press-cross-at 52. This skip option tests the existing retail skip branch; it does not verify movie playback.

Python defaults to muted audio, validates ELF/session/PID, caps runs at 360 seconds, and stops only its own runner. It records up to 16 distinct missing function targets and preserves read-only VU state in each compact snapshot. Outputs overwrite logs/inspector/. Use tools/run-inspector.ps1 for an interactive inspector session; close it before any automated test. See INSPECTOR.md for watches and capture options.

Use focused probes in tests/ to reproduce a concrete failure before changing portable C++. Inspect live images and state after each meaningful change. Do not substitute successful counters or absence of crashes for visible gameplay.

## Recover original functions

Open the existing retail project with tools/open-ghidra.ps1. The application is <repo>/Ghidra; project data stays in ghidra/projects/MKSM_NTSC.gpr. Use ghidra/scripts/InspectMKSMFunctions.java for instruction/function review and InspectMKSMDataRefs.java for data references. Read-only headless inspection uses -process SLUS_210.87 -noanalysis -readOnly with scriptPath pointing to ghidra/scripts. RecoverMKSMEntries.java exports reviewed boundaries to ghidra/recovered-entries.csv.

Append only verified retail boundaries to config/kernel-functions.csv, then:

```powershell
.\tools\generate-supplement.ps1
.\tools\build.ps1
python tools/run-cmake.py --build <repo>/out/mk-runtime --config Release --target mksm_debug_check --parallel 1 -- /nodeReuse:false
& <repo>/out/mk-runtime/ps2xRuntime/Release/mksm_debug_check.exe
```

Main generation uses config/recompile.toml and tools/generate-main.ps1; build.ps1 -Generate regenerates main and supplemental sources. Staging goes to runtime/staging-output/ and only changed sources are copied into runtime/output/. Do not edit generated translations as the long-term fix. Keep authored hooks/headers in runtime/; update the generator or verified function map when appropriate.

## Retention and current work

Keep the existing three build trees (out/tools, out/mk-runtime, out/mk-iop-checks), reuse reports and overwrite captures. No backups. Preserve game assets and ELF identities. README.md is the current checkpoint; SFSV-RPC.md preserves reversing evidence. Automated Python captures remain muted by default; interactive playtesting is audible as requested. Audio quality is still under investigation.

Frontend direct voices and sound-bank teardown are now implemented. Run `python tests/probe-sndf-init.py` for original IOP setup, register, start/stop and bank-unload references; `python tests/probe-sndf-adpcm.py` for the bounded real-sample fixture; and `python tests/probe-sndf-pitch.py` for the retail soft-double log ABI. Rebuild the runtime before out/mk-iop-checks, because the latter links the runtime build's IOP library. Eighteen IOP checks and the native runtime checks pass.

## Character-to-movie checkpoints

Expected progression: Liu Kang confirmation and spoken name, choose Normal, play opening.sfd (Start can skip), Loading, then immediately the in-game Goro's Lair introduction: level title, Liu Kang and Kung Lao falling into the cave, and Raiden dialogue. The first playable stage follows that cutscene. The cutscene itself is the next visible checkpoint; a HUD atlas is a failure.

Earlier September 26 recovery added 75 missing original retail callbacks and a corrected translation of graphics wait 0x24e680, bringing the supplemental map to 681 ranges including the required boot entry. Physics recovery covers box-shape and broad-phase methods, followed by geometry-sort and player/game-object interfaces. tests/find-physics-vtables.py now reports zero registration gaps in its bounded 36-table audit. This is not a whole-game coverage claim. Prototype identities and callback/callsite evidence are in PROTOTYPE.md.

The shared COP2 branch emitter had confused arithmetic STATUS bit 0 with VPU_STAT bit 8 (VU1 running). It caused the observed 0x24e770 loading wait. Python reproduced the false wait; the generator regression failed before the fix and passes afterward for all four COP2 variants plus all 12 signed branches. Twenty-four native original-retail wait cases pass, including truly busy states. The corrected retail wait and its resume entries are registered over the stale main translation. No guest busy flag or readiness result is forced.

Further recovery restored 157 additional original callbacks, taking the map from 681 to 838 rows including the boot entry. HUD descriptors, fade completion, object interfaces, actor queries and vector-related methods are now registered. Python table audits report zero gaps in 53 identified HUD targets and 23 live object tables plus the specifically observed dynamic callbacks. Ghidra independently verified every new range. This is bounded coverage, not whole-game completeness.

A false null call was traced to a missing predicate at 0x36d548: its real zero return skips the optional callback at 0x31afac. The original predicate is restored and the null report disappeared in a later run below the diagnostic cap. No address-zero handler or guessed readiness values were added.

SPR DMA now masks MADR bit 31 on register writes and preserves the remaining alias bits on completion. The original particle producer explicitly writes that high bit; the old runtime rejected the transfer. Python verifies the retail trigger and native ps2_spr_dma_check passes 30 data/address cases plus disabled/invalid-range checks. The former 0x20c614 -> 0xc0091 cleanup failure disappeared after this correction.

Three later original callbacks were restored: 0x121038 (particle record), 0x3ccf70 (closest hit), and 0x46f768 (collision result collector). The map now contains 841 rows; the expanded 26-table live audit has zero known registration gaps. This remains bounded coverage.

The previous negative physics-bound failure is preserved in logs/broadphase-bound-probe.json: original loop 0x3a0380 held s7=-57555, making its signed comparison against every unsigned halfword repeat. The probe verifies all 65,536 entry values. Its old snapshot has since been overwritten; do not rerun that checkpoint-specific probe against a progressed capture and interpret the rejection as a regression.

Shared macro VSUB now preserves hardware VF0 and updates MAC/STATUS flags, including discarded destination-zero writes. Python generated 21 scalar fixtures; the native regression passes 1,344 mask/destination cases, and the generator passes 224 decoded variants. tools/refresh-vu-sub.py verified original retail opcodes before replacing 842 stale statements across 243 generated files. logs/vu-sub-refresh.json preserves the migration list. New generation emits the helper directly. Other macro arithmetic flags and micro-mode timing are outside this correction.

Live inspection then exposed missing VF0.w initialization in fresh guest threads and interrupt invocations. Thread start/restart and GuestInvocation initialization now set (0,0,0,1). Both defects failed native regressions before correction and pass afterward. The interrupted-stack and compiled inspector checks also pass. No guest physics bound is clamped and no loop is bypassed.

Six further original retail callbacks were recovered: 0x14ef20 (object initialization), 0x170b58/0x1720e8/0x1737b0/0x173e18 (HUD/status/combo/XP drawing), and 0x1e6958 (record-state notification forwarding through 0x29a258/0x29a360). Ghidra verified each boundary and overlap checks passed. The map now contains 847 rows including the boot entry. The exact semantic name of the final event remains unconfirmed; it is not replaced by a guessed success response.

The completed 420-second full-opening checkpoint used runner SHA256 7952674f90739230d4853a8aff560b3f1ec428a73161a5a6a5654d8e80b58d26, session 006e7562d80d430b9197a3a43a8af466, with 1,514 fresh snapshots. No intro-skip flag was set and no Start input occurred after the movie began. Normal difficulty was confirmed in the profile. Opening playback served 6,792 pictures and its decoder shut down around 303 seconds. The test then reached Liu Kang's HUD and distorted scene drawing. No missing targets or fatal-runtime-error lines were recorded; the 16-target cap was not reached. No sampled VF0 anomaly or negative broad-phase bound was observed. The harness stopped its own runner at the deadline. These are bounded observations, not exhaustive coverage or audible verification.

Visible result remains distorted dark geometry against a mostly white scene. This does not validate Goro's Lair title, falling characters, Raiden dialogue or gameplay. An unsupported SFSV batch warning for command 1 remains visible; whether it blocks the in-game cinematic is not established.

The new delayed triangle capture holds 4,096 triangles from 302617..344730 milliseconds; 1,747 bounding boxes fall outside scissor, with no nonfinite values. This sample includes transition drawing, so its counts are not a controlled improvement comparison against earlier stage-only captures. The VU1 input capture is exactly 267,488 bytes: seven starts at PC 0, followed by PC 0x790 / TOP 24 with a different microcode image, around 301472..301534 milliseconds after first VU execute. All eight VF0 values are correct. PC 0x790 loads setup vectors and ends; these captures are not a complete geometry replay. Python checks validate session and file hashes before overwriting logs/scene-triangles-probe.json and logs/vu1-starts-probe.json.

Retail SDK normalization, NormalLightMatrix, InnerProduct and single-vector RotTransPers semantics were corrected against original instructions. ps2_vu_sdk_check passes 39 finite-input/alias fixtures. The trace did not show nonzero-W dot contamination, so these are correctness fixes without a proven scene-causality claim. See PROTOTYPE.md for exact addresses and limits. The next investigation compares original view transform 0x660d60 and projection matrices with VU program inputs/outputs, then verifies the full in-game introduction and controls.

Reuse logs/first-stage-relinked and its inspector/report/frame. The failed initial timing attempt remained at the title screen because Start was too early; it was stopped and overwritten. The completed run used Start at 40 seconds and Cross at 12, 16, 20, 46, 58, 71 and 84 seconds. Inputs are test timing, not a fixed game requirement. Only three existing build trees remain. Histories and captures stay capped.

Prior GIF investigation verified header-only IMAGE packets followed by matching payload continuations. tests/probe-gif-stream.py recognizes that exact adjacent pair and rejects isolated truncation. The current run did not request GIF capture; do not analyze a stale gif.bin against its report.

### Latest culling correction and packet investigation

Python and the original compiled retail sphere test reproduced swapped VCLIP plane bits: an inside sphere returned outside, as did an intersecting sphere. The portable ps2_vu_clip.h helper corrects the plane order, absolute-W/denormal comparison and 24-bit history. All four sphere cases and 34 scalar/alias cases now pass. The rebuilt shared recompiler passes 16 decoded VCLIP combinations along with prior branch and VSUB checks. Fourteen original-opcode-verified generated statements were refreshed across five files; logs/vu-clip-refresh.json retains the sites.

Post-correction runner c1be16b1fc71bfb4bfca595d1b68d2251c7e460e0eebad28fdabc24fb1f1b324, session 5013155604664ca3b944573fc0cbfbdd, ran for 210 seconds with intro skipping and 339 fresh samples. No missing-target or guest fatal-error lines were recorded, but VU processing did fail repeatedly and the visible result was black. Newly captured VU entry/resume PCs include 0x3808/0x3af8, 0x17e0/0x1870 and 0x1bc0/0x1c68. Captured code has no 0xfffffffb word: that misleading “reserved instruction” marker comes from XGKICK exceeding its fixed 64 KiB packet capacity. Do not call this a clean run or a repaired scene.

The diagnostic rebuild completed and the original retail culling check still passes (four sphere cases, 34 scalar/alias cases). Live captures repeatedly identify issue PC 0x36c0, failure PC 0x36d8, source 0xdc0 and bad tag offset 48. First tag is valid; the following tag is `67f22eb167abb3f81204000000000000`, requesting 468,608 bytes. The packet limit has not been raised and failure handling remains unchanged.

The portable `ps2_vu_replay` target and Python wrapper now allow bounded offline experiments. Session f2e6b44a59344740a25b7c0233178db5 captured the last eight input calls before failure. Seven calls begin at resume PC 0x1298 with a valid header at VU data 0xdf0. The final fresh call at 0x35e8 begins with that header already damaged. Replaying the failing input reproduces the exact fault in 44 cycles; replaying the immediately preceding input finishes in 1,518 cycles with a valid header. Empty replay pipeline queues limit what this rules out. It does not prove the live previous call was correct.

The command-stream capture resolved the boundary: baseline session f953af491cc6484d87441b530ec4cefa, runner 47edd4bea92b62fe29cdd49dbf148aeccf1362185c638cc05944984620d83e37, contains a 1,419,984-byte VIF stream. The bounded prefix is 588,308 bytes. At 0x8ee8c, DIRECT declares 16,384 payload bytes. The supposed MSCAL at 0x8fa10 (`0x142106bd`) is inside that texture payload. Python's legacy parser reproduces the bogus launch exactly; respecting DIRECT boundaries does not execute it. See logs/vif-launch-probe.json (hash-bound baseline, retained separately from the latest live report).

Root cause: pending GIF IMAGE data was consumed at the top of the VIF command loop, swallowing NOP/DIRECT command words and losing alignment. The portable C++ fix consumes pending IMAGE bytes only inside subsequent DIRECT/DIRECTHL payloads. VIF control words continue to execute normally. A helper recognizes a trailing partial IMAGE after preceding GIF register packets. This agrees with [PCSX2's DIRECT handler](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Vif_Codes.cpp), whose transfer count comes from the DIRECT immediate rather than GIF NLOOP.

Native ps2_vif_image_check failed three of four initial fixtures before the fix (including five unintended VU calls where one was expected). The initial five fixtures pass: inline upload, header-only split, separate VIF calls, partial inline IMAGE with DIRECTHL continuation, and packed GIF registers preceding a split IMAGE. Pixel bytes, VIF MARK/ITOP and exact VU launch count are checked. The old broad-suite fixture that placed raw image bytes outside DIRECT was corrected to use a real second DIRECT command.

Live validation completed: session c94c9940b09049d0997a2ba22c015fad, runner f0fa447a940334d2eed02041989ded1bace48d384e75d0186d1ca5f628587de9, 180 seconds with intro skipping and 474 fresh inspector samples. No XGKICK failure, missing target, recorded runtime error, VF0 anomaly, negative broadphase bound or heap anomaly was detected. The saved frame shows Liu Kang's HUD and textured scene geometry, with large white areas, striping and distorted geometry. This is visible progress beyond the black screen, not verified gameplay or the intended Goro's Lair introduction.

Next rendering investigation: capture delayed scene triangles, effective GS texture/depth/display state and VU inputs; separate invalid geometry from texture/framebuffer problems. Full opening playback and the intended in-game introduction must be rechecked after scene rendering advances. Keep the packet limit and failure stop; no bypass was added.

Transport follow-up implemented: explicit remaining-DIRECT state buffers a declared payload across processVIF1Data calls (bounded by the 1 MiB DIRECT maximum), including splits within the GIF header and pixel bytes. Reset/FBRST clears pending data. Four added fixtures failed before this change and now pass; all nine native cases passed again after the full runner rebuild. This does not claim support for a split within a VIF command word. Never consume arbitrary VIF command bytes as IMAGE data.

Further rendering investigation used the previous f0fa447a runner: session a13d675d147346ec98b9ddbc963645e8 captured 4096 triangles with no nonfinite or degenerate triangles; session 7a947f1c39f84b5e99ad2b4bebb8bbf0 captured all draw types and found tiled screen effects sampling texture base 4480, the same byte region as depth base 140. This may be intentional scratch-buffer reuse and is not proof of the white-area defect. Existing scene-triangles-probe.json and scene-draws-probe.json retain their own capture identities. White pixels occur in both display fields. Next checks target actual stick reads, camera response and pixel provenance; do not infer playable gameplay from an active HUD.

For the full muted trace:

```powershell
python tools/debug-runtime.py --seconds 100 --trace-frontend --press-cross-at 12 --press-cross-at 16 --press-cross-at 20 --press-start-at 24 --press-cross-at 30 --press-cross-at 42 --press-cross-at 55 --press-cross-at 68
```

For the visible difficulty menu, omit Cross at 68 and stop at 64 seconds. Input timing is only a test schedule: inspect the live frame and report.json/frontend_samples. The trace uses all 16 watch slots and retains at most 48 changes; omit --trace-frontend when using extra movie-state watches.

Retail 13b3b0 requests the selected character announcement. Character confirmation reaches 131240, constructing dialog 5ca0c8. Update 1306d8 saves selection+1 through 1daf90 and schedules 131050 (single player) or 12fe18 (co-op). The latest trace proves these stages and sound-manager drain separately. The full opening timeline and transition to Loading are verified. Audible announcement quality, successful level loading and gameplay are unverified.

Playback uses linear interpolation and the observed ADSR path. Reverb, Gaussian interpolation and sequenced SNDF commands remain unsupported. Keep new commands visible until behavior is validated against the original instructions; never acknowledge them as successful merely to clear a stall.

Full opening verification uses the same input schedule with `--seconds 345`: approximately 70 seconds of frontend plus the 230-second movie and time for the level transition. No Start pulses occur after the movie begins. The duration ceiling is 420 seconds; snapshot histories remain capped at 48 changes. `tests/probe-cri-pcm.py` executes original IOP conversion and source-return instructions and reproduces the restart ownership gap. `tests/probe-cri-clock.py` derives the active codec from a `movie_header` watch instead of assuming Midway's allocation.

Live split-transport validation: session 561a3b0cbd7e41e7aadd6d832d906a1d, runner b7e0f2822778f4e0fdd5377d9563e0e21b2370647b6b7be60127b27bc00eabd4, completed 185 seconds with 501 fresh samples and no recorded XGKICK failures, missing targets, runtime errors, VF0 or heap anomalies. Both left-stick extremes and neutral were observed in guest pad reads. Camera remained at (5001,-2205,-4300) through the first pulse, then transitioned around the second pulse. This overlaps the scene transition and does not establish player movement. The subsequent run overwrites movement-probe.json with the latest bounded movement evidence.


Latest rendering checkpoint: session 95742f6cffa04237987d9b16d9273b2a, runner 4aff927d0873741b57195dc5373bd4da302b53034437016ec2a2064c427863b6, 155 seconds, 429 fresh samples, no recorded runtime errors or XGKICK failures. Pixel capture SHA c328913e7e7ce519cd9bcf0e76a6a07ba01d240a147db6c63b06c9c2cd3a5b99 has 1024 successful writes at two fixed GS pixels. scene-pixels-probe.json is hash/session bound. All captured draws have COLCLAMP=1; sampled white-producing bloom sprites have fog disabled. The texture base 4480 and framebuffer base 140 alias the same memory. Repeated additive sprites (ALPHA 0x1700000068) increase sampled channels toward 255 before the result is composited into buffers 0/70. This identifies the stage of corruption, not yet the exact hardware behavior fix.

Texture feedback is the next focused investigation: CPU SampleTexture reads live VRAM; TextureFlush is currently a no-op. [PCSX2 GSState HandleAutoFlush](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSState.cpp) describes page-buffered texture reads retaining old pixels during writes. [PCSX2 GameIndex](https://github.com/PCSX2/pcsx2/blob/master/bin/resources/GameIndex.yaml) sets SLUS-21087 autoFlush=2 specifically for bloom intensity. Do not blindly snapshot each primitive or suppress bloom: identify page/TEXFLUSH boundaries and validate with Python plus a native feedback regression. Prototype-map candidate post-processing matches are retained separately in ghidra/bloom-candidates.json; these are not verified symbol imports.

Longer input checkpoint: session 627d7ba5e6fa4c75af7067dcbf4f0a69, runner d888c5ffb932df7b28c77b5562b7a5a9d7f729e728a6b3dd77b6c2c67ff3b83d, 240 seconds, 627 fresh samples. Both later stick pulses reached guest reads and camera coordinates changed, including motion after neutral. The final image shows a HUD and heavily corrupted scene. Without actor-position or visible controlled-motion evidence this does not verify gameplay. Its bounded stick samples are retained in logs/movement-probe.json. No runner or build remains active after these checks.


## 2026-10-09: Feedback baseline and limits

Revalidated captured session a49cb286f53f4452bb2c0c2d73cadae1 against all three file hashes and runner fc3a4c89e50385a1416118b689a68bdbadbdbc349b8195b50c47354cfbf42af7. Coherent Python replay matches all 1,048,576 VRAM words after 840 sprite draws and 855,296 writes. Page-cache, flush-snapshot and draw-snapshot hypotheses differ in 8,363 / 8,970 / 8,921 words respectively. Whole-VRAM white counts are 162,855 coherent versus 162,661 / 162,520 / 162,542: these are hypothesis comparisons, not proof of correct hardware behavior. No cache hypothesis is promoted to C++.

Surface-specific check: framebuffer 0 already contains 48,195 exact-white pixels and framebuffer 70 contains 49,851 before this late feedback pass. Neither changes during the captured pass. Scratch framebuffer 140 rises from 11,857 to 14,278 exact-white pixels (640x224 region). All captured UV/XYZ endpoints are integral; fractional XYOFFSET values are present. The next investigation captures the earliest matching bloom pass rather than assuming this late pass explains initial saturation. Earlier frames may have contributed. Captures remain bounded and overwritten, with evidence summarized here.


### Earliest first-stage feedback pass, same runtime

Fresh 145-second muted run, session 531c17d0c46f40f8acd18175584b5204, same runner fc3a4c89e50385a1416118b689a68bdbadbdbc349b8195b50c47354cfbf42af7: 417 fresh inspector samples, no reported runtime errors or XGKICK failures. Used the documented diagnostic intro skip and timed inputs; this is not natural opening-movie or gameplay validation. The earliest matching pass was flush 37442. Local framebuffer inspection confirms cave geometry, not a menu or Loading screen.

Before this pass, exact-white counts on framebuffer 0, 70 and scratch 140 were all zero. Coherent Python replay again matches every native VRAM word; after the pass scratch 140 has 588 exact-white pixels. Page-cache and flush-snapshot hypotheses yield 527 and 515, with 3,455 and 4,009 mismatched words respectively. These differences do not establish hardware-correct behavior. No speculative C++ cache fix was applied.

The final live image remains heavily overexposed and geometrically incorrect; the original Goro's Lair sequence and actor control are unproven. Next: trace the post-bloom composite and framebuffer clearing across early scene frames, and investigate fractional XYOFFSET handling with a targeted raster reference. Distinguish intended bright-source bloom from erroneous accumulation before changing runtime behavior.

Capture hashes: CSV b52d2184da9972eab665f2eb358825dcee5832e6542b2d11b4d90156c6c6e033; start 4e6ebdbde55857269ec03bc34cb5c20bd063ba9676bc5ca8362e00fe979f74a3; end 0bd7213436c85f6f05ec43f26f06a3095086f24523a22c2619e2c10dd378a877. Raw data remain local in the overwritten bounded capture.

## 2026-10-09: Capture the final bloom composite

Retail decompilation identifies the blend builder at 0x278050, XYOFFSET_2 builder at 0x278520, feedback helper at 0x279f08, packet setup at 0x276ca0/0x276f00, and final composite helper at 0x27cf38. Observed blending constants agree with the builders. Prototype gg_off3/gg_off4 layout is consistent with the watched 0x65a5c0/0x65a5c4 values (64 and 16); these are not proven packet headers. Call relationships and limits were added to ghidra/bloom-candidates.json.

The previous capture ended on framebuffer change and therefore omitted final compositing. The C++ diagnostic now continues supported CT32 sprite batches across that change, stays capped at 4096 draws, and emits a small hash-bound stop record. Snapshot reads on BeginTransfer now occur under the backend mutex. Rendering behavior is otherwise unchanged. The same existing build was reused; FFmpeg remains enabled.

Fresh run: session 8a9e8826e8c04553b173a9663074767c; runner 5e1aa6f51b312043a2de47e5130df99888b4a864439057fcfc70898595bbd110; 145 seconds, 413 fresh inspector samples, no reported runtime or XGKICK errors. Diagnostic intro skip was used; this does not validate the natural cinematic path or gameplay.

Captured 1120 sprites, ending before a triangle-strip batch targeting framebuffer 0. Python coherent replay matches all 1048576 VRAM words after 1142016 writes. Final composite starts at draw 840 (scratch 140 -> framebuffer 0, fixed blend factor 32); draw 980 begins an untextured alpha-only pass. First-frame framebuffer-0 mean RGB changes from 5.513086 to 7.806410, with zero exact-white pixels before and after (six near-white afterward). Scratch 140 has 605 exact-white pixels at the end. The final 145-second image remains overexposed. The first composite does not itself explain later saturation; next trace frame clearing, depth writes and later compositing together. No speculative rendering fix is enabled.
