# Prototype symbols and retail reversing - 2026-09-20

Paths in this document are relative to the MKSM workspace root unless stated otherwise. Current commands: [WORKFLOW.md](WORKFLOW.md).

Retail remains the executable, asset and gameplay base. The prototype supplies names and comparison evidence; none of its executable code or assets was transplanted.

## Useful symbols recovered

39 reviewed symbols are imported into both Ghidra programs under MKSM_Recovered: 24 functions and 15 globals. The existing project is ghidra/projects/MKSM_NTSC.gpr; retail is /SLUS_210.87 and prototype is /prototype/SLUS_210.87.

Priority reversing names now include Memory_Initialise, Memory_ResetBlock, Memory_AllocBlock, Memory_SetBlock, Memory_Available, Memory_Used, Memory_InitMallocBlockInfo, Memory_StatsString, GetAsyncFileSize, Async_DataLoad, AsyncLoadStatus, AsyncProcessLoad, StartAsyncLoadFile, and the cPxFileInterface loading/status/close wrappers. Named globals include alloc_blocks, alloc_block_f, async_info, async_load_info, file_info and gPxFileInterface.

The recovered ALLOC_DEF layout is 104 bytes; alloc_blocks contains 128 entries. Ghidra has the typed array, and Python decodes its SYSTEM_MEMORY entry. Field names are reconstructed from instructions, not original compiler type metadata. Python understands KSEG address aliases and reports implausible lengths separately.

ghidra/retail-symbols.json records exact ELF hashes, both addresses, body hashes, reviewed instruction differences and semantic differences. ghidra/verified-symbols.tsv feeds the hash-guarded importer. Reproduce with tools/import-prototype-symbols.ps1. Analysis CSV/TOML exports are updated; the active retail recompile configuration is retained.

## A blocker found using these names

Async_DataLoad calls GetAsyncFileSize and then Memory_AllocBlock. The failing request was file ID 35, whose packed retail WAD metadata is 0x803aa0026d400028 at file offset 0x48. Correct decoding gives data offset 0x14000, compressed length 0x9b5 (2485), and an aligned output size of 0x1d50 (7504). This archive uses EWDF payloads; Python did not establish a successful zlib/gzip decode of them.

At guest PC 0x211900, BGEZ must test the low 64-bit signed register. Generated code incorrectly used GPR_S32 and took the plain-entry branch because bit 31 is clear even though bit 63 is set. That yielded size 0x803aa002, matching the live failed allocation exactly. The allocator's signed size comparison then allowed the malformed size to distort SYSTEM_MEMORY. This is a concrete cause of the previously observed later heap anomaly, separate from the earlier IRQ stack overwrite.

Fixed the shared branch emitter for BLEZ/BGTZ/BLTZ/BGEZ and their likely/link variants. Rebuilt the generator, regenerated the existing supplement, and registered a corrected retail GetAsyncFileSize translation at 0x211878. The whole main generated game has not been regenerated; older translations elsewhere may still contain the 32-bit branch bug. Future regeneration uses the corrected emitter. The current targeted fix keeps normal retail archive behavior.

Validation:
- Python read the real WAD entry and reproduced both the wrong branch and correct metadata.
- Native original-retail decoder tests cover packed entries with conflicting bit-31/bit-63 signs, packed entries with matching signs, and an ordinary entry.
- All 12 signed branch variants passed focused code-generation checks.
- A 20-second live run after the fix produced 77 fresh snapshots and no observed heap anomaly. File loading advanced beyond the prior 0x14000 failure to offset 0xc3800; async_load_info reached idle. This does not establish visible rendering or gameplay. Remaining IOP calls and the later wait still need investigation.
- The callback-stack arena still needs the stronger overlap audit described in SFSV-RPC.md.

## Debug features preserved for later

Six retail routines were recovered and compiled with correct entry boundaries: SelectFreeCam, SelectFreezeCam, GodModeToggle, SetGodModeText, DebugMenuSetup and DebugMenuUpdate. Nine Python instruction cases and nine native full-RAM comparisons passed for the two camera routines and god-mode menu text. The full god-mode side effects, debug-menu interactions and gameplay activation have not been tested; no hotkeys or automatic startup toggles were enabled.

Retail player stride is 0x33c0 versus prototype 0x3380. Retail god-mode text IDs are 0x666/0x667 versus prototype 0x50b/0x50c. SelectDebugMenu is deliberately excluded: one short-signature match was an internal block of SelectFreeCam, not a separate callback.

More importantly for reversing, retail Async_DataLoad uses 52-byte requests and packed/compressed-size planning; prototype uses 48-byte requests and a different gzip inspection path. Retail GetAsyncFileSize also has an extra output-size argument and file-ID base 30 versus prototype base 20. Their identities are supported by complete decompilation and call/global relationships, not claimed instruction equivalence.

## Source limitations

Prototype main ELF SHA-256: f0f0234bec88bd0d68912b92d86d75d7e6dc14a877bbb7b0c69843517a653f64. Retail: b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2. Prototype ISO's main ELF matches the extracted copy.

The prototype main ELF is stripped. mka.map has 19,314 nonzero-sized records but does not exactly match that ELF: .text sizes differ, and local offsets vary. Reviewed SelectMenu code is map-8; the reviewed memory/file code is map-0x1a8. These are local correspondences, not universal relocations. Unreviewed candidates in ghidra/prototype-symbols.json and ghidra/reversing-symbol-candidates.json are not imported.

jarvos.elf has 2,144 named functions and substantial debug sections, but is a separate executable whose role remains unconfirmed. Its symbols are not assigned to the main retail game.

Useful checks: tests/transfer-prototype-symbols.py, tests/probe-debug-features.py, tests/probe-wad-branch.py, tools/debug-runtime.py --self-test. Native targets mksm_debug_check and ps2_branch64_check use the existing mk-runtime and tools builds. Logs/reports are overwritten, and allocator trace output is capped.

## Retail callback and scratchpad DMA follow-through

The corrected file-size path exposed missing indirect entries, not just missing IOP services. Recovered retail EWDF dispatch routines (0x466690, 0x4667f8) allow the first packed asset to decode to its header length 0x1041c. The WAD's 0x10428 is aligned allocation capacity, not decoded byte count. Asset callbacks install the resulting buffers.

`tests/find-async-callbacks.py` finds 95 direct registration callsites carrying 56 constant callbacks in t0. Retail 0x379078 stores that argument in the 52-byte load request (or wraps it in 0x379008). Ghidra independently validates boundaries; 52 were newly added after four individually recovered callbacks. Further recovery includes the graphics interface at 0x5859e8, render interface at 0x58a2d0, 30 state handlers installed by 0x1d8620, CRI stream interface at 0x548730, seven-module registry at 0x5ab770 (98 entries), 20 handlers installed by 0x45ca70, and five adapters installed by 0x441060. These translate retail instructions, including genuine retail no-ops where present. Prototype executable/assets do not replace retail behavior.

`tests/find-cri-movie-callbacks.py` preserves the registry audit. `ghidra/scripts/RecoverMKSMEntries.java` checks the exact retail hash, rejects interior entries and unreasonable spans, and exports boundaries. `config/kernel-functions.csv` and generated `runtime/kernel-hooks.cpp` record final entries. `tools/generate-supplement.ps1` verifies every entry was emitted and binds them to the retail identity.

Live execution then threw on `sceDmaRecvN`: channel 0x1000d000, destination 0x75f100, QWC 0x400. Restored original retail 0x384600 and helpers 0x384158/0x3841d0. Fixed shared PS2Memory MMIO support for normal SPR-to-RAM and RAM-to-SPR copies, wrapping SADR within 16 KiB, advancing MADR, clearing QWC/STR and publishing actual completion/status. Previously neither channel copied bytes. The general host `sceDmaRecvN` stub remains unimplemented; this retail profile executes its original SDK routine.

Validation: Python SPR probe passed the observed 16 KiB transfer, wrap, multiple wraps and zero count. Native `ps2_spr_dma_check` passed eight content/address cases plus disabled-DMA and invalid-range checks. `mksm_debug_check` passed 13 fixtures, including original SDK receive of all 16 KiB. Signed-branch checks passed all 12 variants; IOP checks passed 10/10; Python/compiled-writer inspector checks passed. All use existing build trees. The SPR change supports synchronous normal transfers to EE RAM; chain, interleave, MFIFO, VU destinations, precise timing and enabling a previously disabled pending channel remain unsupported. Unsupported SPR modes/ranges raise errors instead of reporting false completion.

The receive fix advances the live run into later CRI setup. A 30-second checkpoint yielded 114 fresh snapshots, no observed heap anomaly or RPC sequence gaps, and DMA starts increased from 30 to 34. Captured output remained black. Remaining callbacks, IOP/movie/render behavior and controls/gameplay need validation. Read logs/inspector/report.json for the newest run instead of treating these historical counts as current.

SPR address progression was cross-checked against https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPR.cpp . This is a bounded synchronous model, not timing-accurate emulation.

Earlier checkpoint (superseded below): 477 supplemental entry starts (including the generator-required boot entry), with worker callbacks 0x43f920/0x43f270/0x43f8c0/0x43f8d8 and the retail movie buffer-swap callback 0x272b50 recovered. The final 30-second run recorded 115 fresh samples, zero observed RPC gaps, no observed heap anomaly and no fatal runtime exception. It recorded 36 DMA starts and 21 GIF copies; the captured 512x448 texture remained entirely black, and movie-swap words at 0x4fbdcc/0x4fbdd0 were zero at the last sample. A worker still hits missing indirect target 0x441430 from 0x450fc8; investigate its registration at 0x4413f0 and the associated callback interface next. These counters show execution progress, not validated frames or gameplay. Current evidence is in logs/inspector/report.json and ghidra/retail-cri-next-blocker.txt.

Reference corroboration for 64-bit signed branch behavior: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/x86/ix86-32/iR5900Branch.cpp . Primary evidence for this fix is the local retail disassembly, original archive bytes and native/live tests.

## Current display and movie checkpoint

485 supplemental starts are now registered. Further recovered callbacks are 0x441430, 0x4510a8, 0x4510c8, 0x451000, 0x451048 and 0x43f288. These clear the previously reported missing-target calls. The other two additions restore original retail `sceGsSetDefDispEnv` (0x382f40) and `sceGsPutDispEnv` (0x3832b8). The former host replacement left PMODE/SMODE2 untouched, omitted retail timing calculations and guessed zero trailing arguments from the stack. The original retail routines use their actual 16-bit GS configuration at 0x4edc60 and t0/t1 arguments. They are identity-gated translations, not a forced framebuffer fallback.

`tests/probe-gs-display.py` independently executes original ELF instructions and checks 12 NTSC/PAL setups, including interlace/frame mode, pixel format and offsets. Its generated fixtures verify all five display words, an uncached RAM alias, untouched adjacent bytes, SP/return preservation, and private-register writes in native C++. `mksm_debug_check` passed 30 fixtures, including two general GS regressions. The reserved-GIF test failed on the old renderer before the correction.

The shared GS frontend no longer maps reserved GIF A+D addresses 0x59/0x5a/0x5b/0x5c/0x5f onto private display registers. `sceGsResetGraph` now applies its existing display setup through MMIO rather than a synthetic GIF packet (whose 0x41/0x42 writes actually addressed SCISSOR_2/ALPHA_1). These address classes were cross-checked against https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSRegs.h and the original retail MMIO stores. The native regression verifies that reserved writes preserve display state while legitimate GIF writes still work, and reset neither submits a fake DMA nor clobbers SCISSOR_2.

An explicit `tools/debug-runtime.py --skip-intro` mode uses the existing retail branch at 0x1d32fc: temporarily set 0x513a04 to 1, execute original intro routine 0x1d32e0, restore the flag. The default is unchanged. Python validates the exact ELF branch; native fixtures check result 3 and restoration for original values 0/1/2. This is an investigation control, not a movie fix or a verified prototype symbol.

The frontend run after the display corrections yielded 47 fresh samples, 4,540 GIF copies, valid 640x448 host presentation, PMODE=0x66 and SMODE2=3, 411 successful reads on each open pad port, no missing-target reports and no fatal runtime exceptions. The inspected frame showed alternating dark-magenta/black horizontal bands; no recognizable menu or 3D scene. GS history showed implausible drawing state (including context-2 PSM 5) and many register writes, so malformed packet generation/transport/decoding needs investigation. Private display-register corruption was fixed; correct drawing was not established.

The active build and `tools/build.ps1` now enable FFmpeg. The Windows n7.1-241205 dependency is SHA-256 pinned and its DLLs are staged for the runner and native fixtures. Python fetched that same archive after CMake's Windows certificate-provider failure. `tests/probe-movie-decode.py` uses the bundled FFmpeg distribution to decode 12 distinct-in-time frames from retail MIDWAY.SFD, confirming MPEG-2 video at 512x384 and ADX audio metadata. This standalone asset test does not prove native playback.

The earlier normal-startup run (25 seconds, 96 fresh samples) reported `ffmpeg_compiled=true`, MPEG initialized, one player, zero players with input, zero feed calls, zero decoder instances, zero queued/served pictures and zero CD-stream bytes produced/demuxed in the MPEG bridge. Movie frame counter at request+0x7c and both swap words stayed zero; the 512x448 presentation remained black. This was before the thread correction below; current raw files are overwritten by later runs.

## CRI worker and video input checkpoint

The shared EE `ResumeThread` syscall incorrectly exposed the scheduler's internal zero success code. Retail 0x4149f0 forwards that result; 0x4154f8 compares it to the worker ID before calling 0x414988, which wakes a sleeping thread. Successful resume must return the thread ID, consistent with the primary Play! `CPS2OS::sc_ResumeThread` implementation: https://github.com/jpd002/Play-/blob/master/Source/ee/PS2OS.cpp . The correction is at the syscall boundary, preserves negative errors, and leaves the internal scheduler contract intact.

`tests/probe-cri-resume.py` executes hash-checked retail MIPS, including delay slots, with modeled BIOS results. Returning zero leaves WAIT-SUSPEND as WAIT; returning the ID executes WakeupThread and makes it ready. It also checks failed resume and plain suspension. `tests/cri_resume_native.h` runs the actual translated retail chain against real HLE syscalls for ready/sleeping cases and checks signed errors. The native regression failed before the correction and passes afterward, alongside the existing 30 fixtures.

The live movie worker previously ran only twice. With the corrected return value it advances into SFD processing and the decode request. Recovered retail callbacks include 0x4452d8, 0x43c948 and the metadata getter groups used by 0x443a38, 0x443ac8, 0x443bf8 and 0x443c90, plus the subsequently reached timing callback 0x4515f8. Ghidra verified their starts/ends against the exact retail hash; the supplement now contains 511 translated functions (the CSV also includes the startup entry). Earlier counts included that extra CSV row.

File I/O is verified independently: the initial SFD buffer at 0xcf5140 matched the source header. The later CRI video ring at 0x105f900 contains a buffer pointer to 0xe499c0, capacity 256 blocks of 2048 bytes, and queued video blocks. `tests/probe-cri-input.py` compares a 256-byte live inspector watch against an independent FFmpeg stream-copy demux of MIDWAY.SFD. It verifies capture session, ELF identity and ring pointer; all 256 bytes match, starting with MPEG-2 sequence header 000001b3. It records runner/source hashes in overwritten `logs/cri-input-probe.json`. This proves the prefix, not the full ring or native playback.

The next decoder integration boundary is CRI's IPU input ring. Retail 0x44cd08 calls 0x44ca98 to copy compressed bytes; 0x44c0d8/0x44c3d8 submit channel-4 IPU_TO DMA, and 0x44c2d8 snapshots its registers. Shared host MPEG currently receives data through AddBs/Demux paths and does not consume this ring. The live worker reaches 0x44cf60 and waits at 0x44d04c for MPEG input. Also verify SDK layout before writing guest state: retail GetPicture uses inner offsets c4/ec/f8/f0/f4; shared HLE currently uses b0/d8/e4/dc/e0. Do not change the shared layout globally without identifying the appropriate SDK variant. The host MPEG Create implementation also contains addresses from another title. No native movie frames or gameplay have been verified.

Final normal-startup validation of the 511-function build: 12 seconds, 45 fresh inspector samples, no missing-target reports and no fatal runtime errors. It was stopped by the harness timeout, not a successful game exit. FFmpeg is compiled in, but the MPEG snapshot still shows one player, zero feed calls/decoders/pictures and a sequence-header wait. The input-prefix comparison passed again. The native CRI regression and existing 30 fixtures passed; inspector writer/schema checks also passed. The subsequent frontend test overwrites the fixed live capture; `logs/cri-input-probe.json` retains the normal-run session and identity evidence.

Frontend check after the thread correction (`--skip-intro`, 12 seconds): 37 fresh samples, 2,967 GIF copies, 640x448 presentation, 307 successful neutral pad reads per port, no missing-target reports or fatal runtime errors. The inspected image has colored rectangular bands and blocks, with no recognizable menu or 3D scene. These reads show polling, not tested player control. Current `logs/inspector/report.json` and `frame.png` are this frontend session; the game was stopped by the owned harness.


## CRI decoder integration and main wakeup checkpoint

522 supplemental retail functions are now registered. Restored original MPEG Create/Reset, their allocation/IPU helpers, queue-count callback 0x425ea8 and audio-counter callback 0x42a900. The supplement generator emits forward declarations before including translations so recovered helper tail calls compile correctly.

runtime/mk-mpeg-bridge.h feeds complete 2048-byte blocks from the retail CRI ring through the existing FFmpeg AddBs path, applying queue backpressure. runtime/mk-cri-video-ring.h validates ring bounds and updates only the consumed head/count; accepted consumption updates IPU_TO progress registers used by retail stop/restore. Shared MPEG state now accepts per-player SDK field offsets, retaining existing defaults for other callers. MKSM selects c4/ec/f8/f0/f4 and executes original retail Create/Reset rather than host initialization with another title's hard-coded guest addresses. A live 104-byte code guard at 0x1717bc matched the original ELF after this change.

Python probe-cri-ring.py checks original retail GetPicture stores and produces independent FIFO wrap, partial-block and backpressure fixtures. Native tests compare the full RAM result and verify SDK field isolation with an injected decoded picture. The original retail main-release routine 0x415580 also revealed that WakeupThread must return its successful target ID: it clears 0x53417c only when that ID is returned. Python models reproduce the zero-return failure, and the native translated-retail regression failed before and passed after the shared syscall correction. iWakeupThread uses the same corrected boundary; internal scheduler return semantics remain unchanged. Primary corroboration: https://github.com/jpd002/Play-/blob/master/Source/ee/PS2OS.cpp (sc_WakeupThread).

Latest 15-second normal-startup live run: FFmpeg compiled in, one decoder, 15 feed calls, three pictures served, one queued picture, no sequence-header wait, no missing targets and no fatal runtime errors. The main wakeup pending flag was zero at capture. The inspected 512x448 frame remained black. Thus guest input reaches the decoder, but visible playback/presentation is still unresolved. Partial/end-of-stream flushing, output layout, presentation timing, audio progression and later gameplay remain unverified. Fixed logs/inspector files contain this run. The native ring/layout and CRI resume/wakeup checks pass alongside the existing 30 fixtures. The current build is available through run.cmd.


## Movie timing gate isolated

The original first-picture handoff completes, but the next picture is rejected because the CRI audio clock is frozen at 0/48000. tests/probe-cri-clock.py replays the exact retail gate using captured fields and confirms that 2000 consumed audio samples would release the candidate at 1000/29970 seconds. This is a read-only counterfactual experiment; no forced clock or skip behavior was added. Retail cri_adxi.irx is imported into the existing Ghidra project, and its RNA/SJRMT/SJX object and DTX protocols differ materially from the generic HLE. docs/SFSV-RPC.md records the complete call chain, tested capture, RPC IDs and original driver layouts for the next sound-service implementation. The latest fixed inspector capture remains black with three pictures served; no new visible or playable milestone is claimed.

## Retail CRI object and completion recovery

This historical checkpoint reached 523 translated retail functions after recovering RNA completion callback 0x422bb0 (92 bytes, verified retail boundary). The game-specific IOP service creates distinct queues, bridges and RNA objects and processes the observed startup batches. Repeated EE SifInitRpc no longer destroys remote IOP state. Queue behavior is validated by 34 original-IOP instruction fixtures; pan by 14 original-instruction cases; focused native suites pass. At this checkpoint PCM output was still missing. The newer checkpoint below supersedes that limitation.

## Visible retail intro and packet-builder recovery

The supplement now has 528 translated functions after restoring original GIF/VIF packet routines 384c60, 384d30, 3850d8, 385178 and 385360. Original-instruction Python probes and failing-then-passing native tests identify mixed host/retail packet construction as a doubled DMA-length bug. Original SDK DmaAddr evidence also identifies the encoded scratchpad flag lost by host sceDmaSend; the shared DMA conversion now preserves that address. See docs/SFSV-RPC.md and ghidra/retail-gif-builder.txt for exact boundaries, tests and scope.

Host PCM output now returns queue ownership after real callback consumption. Normal startup visibly shows the Midway intro: latest session a99b4a864f8b49059c436f07f1915334 has 92 pictures served, 144,000 consumed PCM frames and a moving retail audio clock. Frontend rendering still shows corrupted bands; gameplay and audible fidelity are unverified. This work uses retail executable/IOP evidence and does not enable additional prototype debug features. Existing reports and build outputs are reused.

Subsequent retail VU evidence fixes the frontend corruption: the DMA walker omitted MPG commands from REF-tag upper words, treating the following microcode as VIF commands. Python matches 2,048 captured bytes to retail 49b8b0; native VIF0/VIF1 regressions pass after honoring TTE on all tag types. The latest frontend capture renders the autosave warning and Cross prompt, with duplicated vertical presentation still unresolved. A diagnostic Cross press reaches pad reads, but progression past the warning and gameplay are not verified. Detailed sessions and shared runtime changes are in SFSV-RPC.md. No additional prototype functions were imported in this checkpoint.

## Retail character-selection checkpoint

The September 20 frontend now reaches Single Player character selection with readable menus and rendered Liu Kang/Kung Lao models. This uses the retail executable and assets. The supplement contains 536 unique installed translations after verified callbacks 1e63a0, 377918, 377920, 422630, 422668, 422648, 422680 and the character-confirmation drawing callback 131640. The last group is referenced by original constructor 4223b8; Ghidra read-only reports retain exact instructions and boundaries. No prototype gameplay or asset substitution was used. Character acceptance and subsequent gameplay remain under investigation. Shared libpad DMA publication and INT+FFMD source-height fixes have native regressions; scripted inputs are verified through frontend transitions. Audio distortion is now measured, not fixed; diagnostics are muted. See docs/SFSV-RPC.md for the latest result.

Latest checkpoint: 583 unique supplemental retail functions; all eight observed frontend method tables covered. Character confirmation reaches the original game-start callback, but gameplay remains unverified. See WORKFLOW.md and SFSV-RPC.md.

## First-stage physics callbacks - 2026-09-26

Live loading traces reached retail object tables 0x567a80 and 0x567868. Recovering their missing methods advances execution through Havok collision-shape and broad-phase setup. These are original retail translations; no prototype executable behavior replaces retail code.

The following paired bodies establish local prototype correspondence. For this reviewed group only, prototype ELF addresses are retail minus 0x26030, and the matching mka.map records are prototype plus 0x1a8. The map sizes agree with independently recovered retail boundaries. These offsets must not be applied globally.

| Retail | Prototype ELF | Map identity | Compared bytes |
| --- | --- | --- | --- |
| 0x3a45f8 | 0x37e5c8 | hkBoxShape::castRay | 520; two relocated constant-address words |
| 0x3a4838 | 0x37e808 | hkBoxShape::getType | 8; identical, returns 6 |
| 0x3a48d8 | 0x37e8a8 | hkBoxShape::getAabb | 100; identical |
| 0x3a4940 | 0x37e910 | hkBoxShape::getCollisionSpheresInfo | 28; identical |
| 0x3a4960 | 0x37e930 | hkBoxShape::getCollisionSpheres | 100; two relocated constant-address words |
| 0x3a4840 | 0x37e810 | hkBoxShape::getSupportingVertex | 132; two address words and one relocated call |
| 0x3a48c8 | 0x37e898 | hkBoxShape::getFirstVertex | 12; identical |
| 0x3a2cc0 | 0x37cc90 | hk3AxisSweep::getAabb | 40; one relocated call |
| 0x3a2c08 | 0x37cbd8 | hk3AxisSweep::getAllAabbs | 180; two relocated calls |
| 0x39f9e0 | 0x3799b0 | hk3AxisSweep::updateAabbs | 2900; sixteen address/call words differ |

Python compared complete bodies, not only entry signatures. The non-identical bodies support these identities through instruction layout, sizes and local map order; relocation targets outside this local group still require independent semantic validation before claiming full equivalence. Names are documented here and are not automatically imported into Ghidra. Shared no-op 0x412130 is recovered from the live table but receives no prototype identity from this comparison.

## Physics and sort registration audit

`tests/find-physics-vtables.py` checks the retail hash and scans only 0x5669d0 through 0x567c00 for the pointer-pair layout anchored by the three live tables above. It rejects unterminated candidates, compares exact entry addresses with main and supplemental registrations, and overwrites `logs/physics-vtables.json`. The audit found 36 candidate tables and 48 missing distinct entries. Ghidra independently accepted all 48 boundaries. Their original instruction translations are registered, including real retail no-ops, type constants and the original shared thunk. A repeated coverage audit reports zero gaps in this bounded group; that does not prove all game callbacks are present or executed.

A later live trace reached original sort routine 0x476e00, calling missing comparator 0x281210 from 0x476fc0 on 0x50-byte records. That comparator sorts Z with a +/-3 tolerance, then X, returning the original -1 on equal X. Its tie behavior is preserved. Python inspection of constant a3 setup at calls to 0x476e00 found these additional gaps; Ghidra confirmed parameter cross-references and complete boundaries:

| Callback | Original sort callsites | Size |
| --- | --- | --- |
| 0x24c7b0 | 0x249738 | 72 |
| 0x24cf58 | 0x249de0, 0x24d024 | 156 |
| 0x281210 | 0x28316c, 0x283380; also referenced at 0x281e84 and 0x2834ec | 156 |
| 0x2b5e98 | 0x2b4e70 | 32 |
| 0x3e17d8 | 0x3e1378 | 16 |
| 0x409090 | 0x4046e8 | 24 |
| 0x409048 | 0x40531c, 0x405558 | 44 |
| 0x409078 | 0x405340, 0x40557c | 24 |

These recovered entries translate original instructions and preserve original comparison ordering and return values. Runtime verification status remains in WORKFLOW.md and the reused first-stage report.

## Player-object interface reached after sorting

After sorting callback recovery, the live dispatch trace reaches 0x1c47a4 -> 0x11eb60 through object 0x5b9e20 and table 0x56ddd8. Ghidra confirms that 0x11eb60 reads and returns the original 32-bit field at object+0xb4; it must not be replaced by a guessed readiness value. The same table contains 0x11eb70 (32 bytes, per-record presence query), 0x11eea8 (312 bytes, player-record updates), 0x11efe0 (136 bytes, reset) and 0x11f068 (200 bytes, selected-player lookup with original side effects). All five original retail entry ranges are recovered. Existing 0x11eb90 remains registered. The secondary table at 0x56ddc0 contains the original -4 this-adjustment and 0x11eb70 entry; this recovery changes no table bytes or ABI behavior.

## Loading wait: COP2 branch condition fix

After the recovered callbacks cleared, the 160-second live run recorded 553 fresh inspector snapshots, no missing target reports, and a proper Loading frame instead of the HUD atlas. Main thread remained at 0x24e770. Original retail function 0x24e680 waits for DMA, VIF, VU and GIF completion; its 0x4901fffb instruction at 0x24e780 is BC2T back to 0x24e770.

The shared control-flow emitter incorrectly used arithmetic STATUS bit 0 (`ctx->vu0_status & 1`) for all four COP2 branches. The condition is VPU_STAT bit 8, VU1 running. A zero result from ordinary vector math could therefore keep the generated wait looping while VU1 was idle. This register distinction is corroborated by [PCSX2 COP2.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/COP2.cpp) and [VUmicro.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUmicro.cpp).

`tests/probe-vu-branch.py` checks the retail ELF hash and exact branch opcode/target and records 84 reference combinations in the reused `logs/vu-branch-probe.json`. The added shared-generator regression fails on the old emitter and passes after the correction for BC2F/T/FL/TL. All 12 previous signed-branch cases still pass. The corrected predicate is `ctx->vu0_vpu_stat & 0x100u`; it does not force idle or clear any guest flags.

The verified 312-byte retail function is regenerated as supplemental 0x24e680, including its internal resume entries such as 0x24e770. Only one BC2 instruction was found in the scanned retail code range, and the stale main translation remains superseded by these registrations. Future main generation uses the corrected shared emitter.

The native `mksm_debug_check --vu-branch` check executes the original regenerated wait from 0x24e770 for 24 combinations. It verifies that idle states return, busy states remain at the wait after a bounded scheduler checkpoint, arithmetic zero/other flags do not change the branch decision, and register values are preserved. Its host fixture initializes MMIO memory and allocates the runtime on the heap. The initial fixture lacked that setup and failed; the corrected fixture passes. Latest live transition status is in WORKFLOW.md and logs/first-stage-relinked/report.json. Test success alone does not establish 3D rendering or gameplay.

## Later adjusted game-object interface

The live run after the VU fix reached 0x1dea78 -> 0x1abad8 through table 0x5781e8. Its 0xfee4 entries supply a signed -0x11c this adjustment; original dispatch already applies it. Recovered 0x1ab9b0 (172 bytes), 0x1aba60 (120 bytes) and 0x1abad8 (80 bytes) preserve that ABI. The last method clears four fields in each original 0x14-byte record; the others retain original update/configuration calls. After rebuilding, live execution advanced to callback 0x1816a8 from 0x30fa68. That callback and its HUD family have since been recovered as described below. See WORKFLOW.md for the final runtime hash and visual limitation.


## Retail HUD descriptor callback recovery

The next live call was 0x30fa68 -> 0x1816a8. Original caller 0x30fa38 walks the linked list at 0x6427ec through object+8 and invokes each nonzero function pointer at object+0x204. The recovered 376-byte callback adjusts active/position-related state from globals around 0x5d16d0.

A bounded 16-distinct-target trace then exposed 16 more missing callbacks from that same callsite. Python located 15 of those in HUD descriptor data. Anchors at 0x4c02d8, 0x4c0310 and 0x4c03b8 identify 56-byte records with callback pointer at +0x30. Scale 1.0 and byte-sized RGBA values distinguish these records from arbitrary code-looking pointers.

tests/find-hud-callbacks.py verifies the retail SHA256 and scans only 0x4bc400..0x4c0ba0. It found 53 distinct descriptor callback targets, 49 initially missing. These are candidates until reviewed, not automatic function-boundary evidence. Ghidra independently verified all 49 boundaries plus live-only callback 0x18f8c0. No new span overlaps an existing supplemental span. Their original logic includes visibility, position, sprite selection, color and size updates; no guessed return values or readiness shortcuts were substituted.

Those 50 entries, together with 0x1816a8, bring the map from 681 to 732 rows including the required boot entry. Repeating the bounded descriptor audit reports zero gaps. This does not establish whole-game coverage or visible first-level success. Current live results remain in WORKFLOW.md and logs/first-stage-relinked/report.json.

RecoverMKSMEntries.java now buffers all verified rows in memory and writes its reusable report only after the complete requested set succeeds. A failed boundary check therefore leaves the previous report intact. The report is separate from config/kernel-functions.csv; source recovery entries are appended only after review.


### Dynamically installed HUD and fade callbacks

The 732-row build (SHA256 248b25a65ac6e44f47e7cecb3da0c835ec17cc1e8c86b008275697b54649dd8b) produced 517 fresh snapshots and exposed four more targets at callsite 0x30fa68, below the 16-target diagnostic cap. Ghidra verified these independently:

| Entry | End | Bytes | Original behavior |
| --- | --- | --- | --- |
| 0x180bb0 | 0x180fc8 | 1048 | Map overlay visibility, rotation, player-relative coordinates and UV updates |
| 0x2f7b50 | 0x2f7c38 | 232 | State-dependent overlay visibility, position, size and color |
| 0x2f7500 | 0x2f7688 | 392 | Screen-fade opacity, countdown and visibility/completion state |
| 0x3230c0 | 0x3231d0 | 272 | Vertical UI motion and visibility, updates global 0x51abcc |

Their direct code references corroborate installation outside the bounded static descriptor table. All four original bodies are recovered, with no overlaps against prior supplemental spans; the map now has 736 rows. Fade logic was restored without forcing opacity or completion. Subsequent live verification is tracked in WORKFLOW.md.


### Object interfaces reached after fade completion

The 736-row run advanced past the HUD update group into level-object and actor interfaces. Runner SHA256 5702df14de5a9e72ed4a3569d2b5ad65b201ded5537dd6fb17bb24a0374734b3 produced 513 fresh samples. The final image changed from the atlas to black, which is not visible first-stage success. Missing-target logging reached its 16-target cap.

tests/find-level-object-vtables.py audits only 14 exact table addresses observed in that run: 0x586588, 0x592c30, 0x576ee8, 0x58ce98, 0x579a58, 0x581958, 0x590608, 0x58ce08, 0x591b28, 0x56de58, 0x581970, 0x58ddf8, 0x57e670 and 0x57b090. It validates the zero header/adjustment layout and bounded termination, then compares method addresses with both main and supplemental registrations. It does not sweep adjacent data or switch tables.

The audit found 37 missing methods totaling 6,128 original bytes. Ghidra verified their entries and bounds independently; none overlaps a previous supplemental span. Recovered behavior includes real retail zero/no-op base methods, minimap teardown, object message handlers, and actor query outputs. For example, 0x1c57d8 writes both original output fields from its backing object rather than supplying guessed values. All 37 bodies are regenerated and registered; the source map now contains 773 rows including the required boot entry. Current live outcome is recorded in WORKFLOW.md.


### Further actor and update callbacks

The 773-row build (SHA256 8818b53f7329c795be6292b2b5baf825fcaa9b28af5f6616568dd3eedbd58b0d) advanced to a new set of actor/update callsites, recorded 513 fresh snapshots and again reached the 16-target limit. The image remained black, so the Goro's Lair cutscene was still unverified.

The same bounded audit now includes seven additional live tables: 0x57b070, 0x578898, 0x576c40, 0x580cd8, 0x58cdd8, 0x56d928 and 0x575a38. It also checks exact observed dynamic callbacks 0x29aed0, 0x2c0ff8, 0x1eab58, 0x34d310, 0x2d30d0, 0x11fab8 and 0x3143f0. Ghidra verified 26 uncovered entries totaling 9,188 bytes, with no overlap against prior supplemental ranges. These restore original actor query/update and callback behavior. For example, 0x29aed0 reads the original indexed resource value; 0x165e08 executes the original actor query and returns its output. No query results are fabricated.

The source map now contains 799 rows including the boot entry. tests/find-level-object-vtables.py covers 21 anchored tables plus those dynamic callbacks, while tests/find-hud-callbacks.py covers the previously identified HUD descriptors. These remain bounded audits, not a completeness claim.


### False optional null call after a missing predicate

The 799-row run (runner SHA256 866c0627aea4b0e6d4a98facdbeff114683060a64afbcad58627bbbdf5429f36) captured 536 fresh samples. It reported 14 nonzero missing callbacks and one null target, below the 16-target cap. The image remained black.

The null call at 0x31afac follows the missing predicate at 0x31af8c -> 0x36d548. Retail table 0x574698 has that predicate at +0x3c and zero at +0xdc. The predicate's original instructions are DADDU v0,zero,zero; JR ra; NOP. Caller BEQ v0,zero at 0x31af94 skips the optional call to 0x31afc4. Under the current continue-on-missing policy, the missing call left stale nonzero v0 (0x36d548), causing the caller to enter an unsupported optional branch. Registering address zero would hide the cause and was not done.

The Python object-table audit verifies the exact ELF instructions, table words and branch destination. Ghidra independently confirmed this control flow. Restoring the actual 12-byte predicate preserves the real zero return. All 14 nonzero targets from that completed run are recovered, totaling 2,816 original bytes and bringing the map to 813 rows. Current live retest status remains in WORKFLOW.md. Missing-call policy itself was not changed.


### Vector/object methods reached after the null-call fix

The 813-row run (SHA256 44b8f722c300a39d4e42f156b6c1c46a73bdae855eb1b2cda0f5263e95b08781) produced 471 fresh snapshots. The previous null-call report disappeared; only seven new missing targets were reported, below the 16-target cap. The final image was flat gray, not the Goro's Lair introduction.

Six targets were methods of live table 0x574da0, dispatched from 0x1583f0 and nearby code; the seventh was 0x1b4e90 in table 0x578ae0. The raw trace for 0x158ae4 included infinity/NaN words in an output-side temporary. This is evidence to investigate missing vector outputs, not proof of a renderer defect.

The audit now includes those two exact tables, for 23 total. Ghidra verified 25 missing methods totaling 2,632 bytes; none overlaps earlier supplemental ranges. Methods 0x15ae18/0x15ae98 and related entries retain original path/vector-output calls and original state math. The recovered map now has 838 rows. No NaN clamping, forced camera position or fabricated vector output was added.


### Current cleanup-list corruption checkpoint

The 838-row build (SHA256 08dcd1d5582e1b7d9358452652fbd08cee8de88efb9fba509d8164ff98203ebd) retained 552 fresh snapshots in session 3dd4668163b248008d96548c4448c749. It reports only 0x20c614 -> 0x000c0091, below the distinct-target cap, with a0/s0=0x53535310. All previously observed code-address gaps and the null-predicate failure are absent in this bounded run. The final image is black; no first-stage rendering claim is made.

Original 0x20c5d8 invokes a node callback at +0xc, then updates free/active list links and counts. Its caller chain includes 0x2da270, which processes list nodes through SPR DMA, and 0x2dc8c0, which iterates the producer list. The source of the bad node pointer remains unproven. Current original instruction/decompiler evidence is in logs/inspect-475998.txt; the raw trace is in logs/first-stage-relinked/stderr.log. Do not register 0xc0091, force a cleanup return, or infer a DMA fix without reproducing the bad write.


### SPR MADR masking defect in the cleanup producer

The original producer at 0x2da270 sets QWC=0x14 and writes (node & 0x0fffffff) | 0x80000000 to SPR-to MADR at 0x2da3cc, followed by SADR=0x70000400 and CHCR=0x101. The runtime incorrectly retained bit 31 in the SPR MADR register and then rejected the transfer. PS2Runtime::Store32 catches that memory exception and signals a guest address error; it does not write a fatal-runtime-error log. Thus the previous report's empty fatal-error list did not prove the DMA transfer succeeded.

The SPR channels fix MADR bit 31 to zero on write. [PCSX2 Dmac.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Dmac.cpp) distinguishes this from the SPR-selector bit used by other DMA channels. The runtime now applies that write mask only to SPR0/SPR1 MADR, maps the remaining address to physical RAM for copying, and advances the stored MADR/STADR values without dropping surviving alias bits. Bounds checks and unsupported-mode rejection remain intact.

tests/probe-spr-dma.py verifies the exact retail instruction sequence and three address aliases. The native ps2_spr_dma_check failed before the fix with 'SPR MADR bit 31 must read as zero'. After the fix it passes 30 direction/address/count combinations, including the retail 20-QWC transfer, wrap and multiple-wrap cases, data/guard-byte checks, completion registers/events, disabled DMA and invalid-range rejection. The pre-existing zero-count behavior is retained in that fixture; hardware zero-QWC underflow, chain/interleave/MFIFO transfers and cycle timing are outside this correction. Live outcome remains in WORKFLOW.md.


### Collision callbacks and allocator corruption after SPR correction

After correcting SPR MADR writes, the original cleanup failure (0x20c614 -> 0xc0091, object 0x53535310) disappeared. Three subsequently reached original functions were recovered with independent Ghidra boundaries: 0x121038..0x1210e0 (168 bytes, particle-record creation), 0x3ccf70..0x3ccfb8 (72 bytes, closest-hit collector), and 0x46f768..0x46f88c (292 bytes, collision-result collection). The map now has 841 rows including the required boot entry. The bounded audit now includes 26 live tables, including collision tables 0x568d70/0x56b040 and allocator table 0x569028, with zero known registration gaps.

The 841-row run with runner SHA256 637d6d33d51da8c9361e02ad13aabc16c844b2f954393c91e4de096df0a1d2c7, session 38a43b374a8b4ae7b15b3a43317dea96, recorded 537 fresh snapshots. All three code callbacks were absent from the missing-target trace. Seven invalid targets remained, starting with 0x20a364 -> 0. This is an allocator corruption failure, not another function at address zero.

The allocator global is 0x5b2010, observed pointing to 0xb0a580. Its +0x20 method-table pointer starts as 0x569028. In the preceding 840-row session ba242c9c86804206b2512a8a4e582a4a, an intermediate read captured that pointer displaced to +0x24, with 0xffffffff at +0x20 and other fields shifted by four bytes. The later trace independently reads the bad table pointer from original array-growth routine 0x20a318. Subsequent reads see the allocator global cleared and CRI callbacks replaced with data pointers. The exact corrupting writer is not yet proven.

Original 0x3a2b58 appends broad-phase handle pairs to a growing array; repeated calls lead into 0x20a318. Original 0x40cd80 is the pool allocation method, and 0x20b638 wraps copy routine 0x20fde0. These bodies and the recovered callbacks are retained together in logs/inspect-475998.txt. Trace the earliest corrupted allocator write before treating later audio/data-pointer failures as independent missing methods.

A transient live frame displayed distorted scene-like imagery, then the final frame became black. The 95-second GIF capture contained 512 packets / 92,320 bytes with no packet-boundary anomalies; this does not establish rendering correctness. Goro's Lair title, characters, dialogue and gameplay are still unverified. Current authoritative run evidence remains in WORKFLOW.md and the overwritten first-stage-relinked report.


### Nonterminating broad-phase bound and VF0 investigation

The later copy-traced session 328fc3130ad54279ba5eee25cbb25bde (runner SHA256 650c54c22a30dfe425763075907d7111a112b45c41eae8a62842b50d487ffe02) retained 516 fresh samples. Original bulk-copy wrapper logged no overlap with the allocator. At the saved failure checkpoint, PC=0x3a0380, s7=0xffffffffffff1f2d (-57555), s1=0x5078f4 and node s0=0xb11c70. Python validates the exact retail load/shift/branch opcodes and all 65,536 unsigned-halfword inputs: the negative signed bound makes the repeat condition permanently true. The report is logs/broadphase-bound-probe.json; its input is the current session-checked inspector snapshot.

Generated VSUB instructions also write VF0 despite its hardware constant value (0,0,0,1), and omit MAC/STATUS effects read by nearby CFC2 instructions. All 78 VF0 assignments found across main/supplemental generated output are VSUB, including duplicate translations. The original bounds code uses VF0 in VMAX at 0x39fa98/0x39fa9c before clamping to the 32767.0 constant and converting to integer. A negative VF0 is a candidate source for negative bounds. It is not yet a verified live causal fix. PCSX2's primary implementation discards Fd=0 results through RDzero while still updating MAC/STATUS: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUops.cpp (applyBinaryMACOp / _getDst). Preserve those flag effects in the correction.


### Shared macro VSUB correction

The native old-emission reproduction failed with `FAIL VSUB destination op=44 mask=1 dst=0`. The correction in ps2xRuntime/include/runtime/ps2_vu_sub.h computes the result and MAC/STATUS effects while discarding writes to VF0. It supports component masks, normal/broadcast/Q/I sources, aliased destinations, zero-mask flag clearing, signed zero, underflow and overflow using the runtime's existing operand/result normalization rules. Sticky and divide-status bits survive; VPU running state is untouched. Micro-mode scheduling and other macro arithmetic operations are outside this correction.

Python tests/probe-vu-sub.py generates 21 independent scalar fixtures. The native ps2_vu_sub_check passes 1,344 mask/destination combinations; the shared generator check passes 224 decoded VSUB variants along with previous branch tests. The Python synthetic clamp example reproduces -57555 with a poisoned VF0 and yields a positive bound with constant VF0; it is a mechanism test, not a live-causality claim.

The shared code generator now emits ps2VuSub for normal, broadcast, Q and I variants. tools/refresh-vu-sub.py verified each original retail opcode before updating 842 stale statements across 243 existing generated files, including supplemental copies. The migration leaves other instructions and function boundaries intact; logs/vu-sub-refresh.json records the affected sites. Future generation includes the shared helper directly. The runtime inspector now reports vu0_vf0_bits and vu0_mac_flags for live verification. Read WORKFLOW.md for the final build/run outcome.


### Thread VF0 initialization and newly reached HUD callbacks

The first live VSUB-corrected run (session fa44cfdcc47d474ba905eeb3c90bb590, runner a456879f3d23a19d6ad1091e0207115a45062641d2594443bdd0f5e213bb3c83) collected 508 fresh samples. The prior negative broad-phase bound and invalid allocator/audio call targets did not recur. A Liu Kang HUD and distorted scene became visible. This observation supports the correction but does not prove every physics path or Goro's Lair gameplay.

That run also exposed VF0=(0,0,0,0) on newly started guest threads. EeScheduler::startThread reset the context without restoring VF0.w=1. New and restarted threads now receive the same hardware constant as the main thread. The native start/restart regression failed before this change and passes afterward, alongside the interrupted-stack regression and compiled inspector integration check.

Five valid code targets reached after loading were independently recovered in the retail Ghidra project: 0x14ef20..0x14ef54 (52 bytes, object state initialization), 0x170b58..0x1720e4 (5516 bytes), 0x1737b0..0x173e14 (1636 bytes), 0x173e18..0x1743b8 (1440 bytes), and 0x1720e8..0x1737ac (5828 bytes). The latter four implement HUD/status/combo/XP drawing and text; they are not cinematic commands. Their descriptor callbacks sit beyond the prior bounded HUD audit. All five were added without overlapping existing ranges; the supplemental map now has 846 rows including the required boot entry. The boundaries were verified in the reused logs/inspect-475998.txt; that report is overwritten by subsequent investigations.


The subsequent 846-row run (session 484cf826f062422e8f6bf245bef78aa1, runner 259d0d6a06434eca73f9fbd4753ae0f017e8b44de3feca9ad3fae2df93f69eb2) recorded 520 fresh samples with no missing targets or fatal runtime errors. The final HUD displayed LIU KANG and EXP: 0, but scene geometry remained badly distorted against white. A remaining all-zero VF0 sample at PC 0x415484 led to freshly constructed interrupt invocations. Their contexts also lacked VF0.w=1. GuestInvocation now initializes the hardware constant before dispatch; the native interrupt regression failed before this change and passes afterward. Thread, IRQ-stack and inspector checks still pass. The latest live outcome is recorded in WORKFLOW.md.


### Final callback and live checkpoint

Original callback 0x1e6958..0x1e6998 (64 bytes) was observed at caller 0x34e72c after a record changed state. It forwards the record index through 0x29a258 and 0x29a360; the latter walks the current object list and notifies non-null +0xbc objects. Its exact event name is not yet established. The verified original body was added, bringing the map to 847 rows. The callback was verified in the reused logs/inspect-475998.txt; that report is overwritten by subsequent investigations.

Final runner 1ce54c7c4c4c3307802ae2f3249c6edd8ccae75708af00fb78e98bae7102c1a8, session ff03505643dc49feb1629b9db989baf0: 210 seconds, 656 fresh samples, no missing callback or fatal-runtime-error lines, no sampled VF0 anomaly or negative bound. Visible result remains a correctly labeled Liu Kang HUD over a badly distorted scene. The full Goro's Lair introduction and gameplay remain unverified. See WORKFLOW.md for the capture limits and next investigation.

### Retail SDK vector semantics and geometry investigation

Original retail instructions establish that sceVu0Normalize at 0x3853b0 normalizes XYZ and writes W=0; NormalLightMatrix at 0x385790 uses that behavior for its three direction rows. sceVu0RotTransPers at 0x3858c0 applies FTOI4 to all lanes for mode zero, and FTOI4 to XY / FTOI0 to ZW for nonzero mode. InnerProduct at 0x213e08 sums XYZ only. The host implementations had included W in normalization and the dot product, and reversed the single-vector projection mode. They now follow these verified finite-input semantics. RotTransPersN was not changed; zero-W, overflow and complete PS2 floating-point accuracy are outside this correction.

Python tests/probe-vu-sdk.py asserts the exact retail ELF hash and instruction words, then generates scalar references. ps2_vu_sdk_check passes 39 cases covering finite values, nonzero W, small vectors, zero XYZ, projection modes 0/1/7, aliasing and the dot-product return in both F0 and V0. The original host behavior failed the normalization/projection fixtures; the three dot fixtures failed before the XYZ-only correction. These fixes are not established as the cause of distorted first-stage graphics.

The bounded PS2_TRACE_VU_MATH diagnostic found calls to TransposeMatrix and InnerProduct. No nonzero-W dot contamination was observed in the 180-second pre-dot-fix run, and the SDK Normalize/NormalLightMatrix/RotTransPers/RotMatrixY traces did not fire. Retail 0x213e30 is a different, generated normalize routine that preserves input W, so it must not be conflated with the SDK routine at 0x3853b0. The original 0x2ebc20 builds orthogonal vectors at 0x511340/50/60 using dot, subtract, normalize and cross product. The call at 0x236ac0 belongs to 0x236878: it constructs the view transform at 0x660d60 from position 0x6cbad0 and rotation 0x6cbae0, multiplies it by projection-related matrices, then updates this basis. Current logs/inspect-475998.txt contains 0x236878, 0x2ebc20 and 0x2eb4d0. These concrete addresses are candidates for the next bounded camera/microcode comparison; no camera state is patched.

Delayed triangle capture proves real perspective triangles are being submitted, but does not prove correct scene geometry. The initial 4096-triangle sample had 3269 bounding boxes outside scissor and no nonfinite coordinates; the dominant perspective texture was TBP0=9616. Large projected coordinates alone do not establish a projection defect. The new bounded VU1 execute-input capture supports examining the actual microcode/data that produced these draws.

### Macro VCLIP plane-bit correction

Shared generated VCLIP had reversed positive/negative plane bits and compared against signed W. The runtime's micro-VU CLIP already used the correct order and absolute-W threshold. PCSX2's primary reference, https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUops.cpp (_vuCLIP), confirms positive X/Y/Z at bits 0/2/4 and negative X/Y/Z at bits 1/3/5, absolute W with the denormal threshold rule, and a 24-bit history.

Retail 0x22c838 consumes those bits with masks 0xaaa and 0x555 to return outside/intersecting/inside. Its fast-path selector at 0x4f41b0 is 1 in the retail ELF. The native original generated function rejected both an interior sphere (expected 1, got 0) and an intersecting sphere (expected 2, got 0) before the correction. The four native sphere fixtures now pass. Python tests/probe-vu-clip.py generates 17 scalar references; 34 native source-alias/flag-preservation cases pass, and the shared generator passes 16 decoded source-register combinations.

The portable helper ps2_vu_clip.h fixes this instruction without changing camera positions or bypassing visibility decisions. The generator now emits it; tools/refresh-vu-clip.py verified original retail instruction words before replacing 14 VCLIP statements in five existing generated files. logs/vu-clip-refresh.json keeps the sites. The first live run after this correction reached additional VU entries (0x3808/0x3af8, 0x17e0/0x1870, 0x1bc0/0x1c68) that were absent from the prior capped sample. However, the visible result became black. Runtime logs reported sentinel 0xfffffffb as a reserved instruction; source inspection shows this actually means XGKICK packet capture exceeded its 64 KiB capacity. It is not a fetched microcode word. The captured microcode contains none of that sentinel. The next blocker is the graphics packet path; do not describe the scene as fixed.

### September 26: false VU launch from texture payload

The XGKICK failure was traced to VIF DIRECT parsing, not a new retail microcode target. In baseline f953af491cc6484d87441b530ec4cefa, byte 0x8fa10 is texture data within DIRECT at 0x8ee8c; the old pending-image path interpreted it as MSCAL 0x35e8. Python reproduced the legacy walk. Runtime continuation now consumes only DIRECT payloads; five native upload/control-boundary cases pass. See WORKFLOW.md for capture identity and live verification status. Do not add 0x35e8 as a missing game function or treat it as an intended geometry entry based on this failure.
