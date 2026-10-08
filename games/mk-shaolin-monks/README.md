> Public contributor setup: [Build](../../docs/BUILDING.md), [Current status](../../docs/STATUS.md), [Contributing](../../CONTRIBUTING.md). The checkpoint text below is historical.

# Mortal Kombat: Shaolin Monks recomp



Start here: [workflow and commands](docs/WORKFLOW.md). Double-click **run.cmd** to test the latest build. Close the game before rebuilding. Interactive playtesting is audible by default; Python diagnostic runs remain muted. Audio quality is still under investigation.



## Folders



| Folder | Contents |

| --- | --- |

| runtime/ | Game hooks, compatibility headers, generated retail C++, memory cards and debugger window settings |

| tools/ | Build, generation, Python runtime inspector and Ghidra launch scripts |

| config/ | Recompiler/analyzer TOML, verified supplemental function boundaries and manifest |

| ghidra/ | Retail/prototype projects, analysis scripts, symbols and reversing reports |

| tests/ | Focused Python probes and native regression fixtures |

| docs/ | Workflow, runtime inspector guide, prototype evidence and investigation history |

| logs/ | Reused build/test reports and bounded inspector captures |



Shared PS2Recomp sources remain at <repo>/ps2xRuntime, ps2xIOP, ps2xRecomp and ps2xAnalyzer. Ghidra is installed at <repo>/Ghidra. Existing builds remain under <repo>/out (tools, mk-runtime, mk-iop-checks).



## Current checkpoint - September 26



Retail remains the executable and asset base: SLUS_210.87, entry 0x0011C070, SHA-256 B1ED99D81B07969553ED2F9AFAAD4CAED1BBEB637D53A369F4DAFCA8EEDD2AB2. The supplemental map has 847 verified retail ranges, including the required boot entry; prototype symbols provide reviewed names and comparison evidence.



Normal startup plays Midway, reaches the autosave warning, and proceeds through the title, main menu and Character Select. Character confirmation brings up the visible Normal/Hard difficulty dialog. Choosing Normal saves difficulty 1. The next visible checkpoint is Liu Kang confirmation and spoken name, Normal difficulty, opening.sfd (Start may skip), Loading, then the in-game Goro's Lair introduction: level title, Liu Kang and Kung Lao falling into the cave, and Raiden dialogue.



**The SFSV sound-drain blocker is cleared.** Frontend direct voices now play real uploaded ADPCM samples through pitched/enveloped PCM with completion tied to host consumption. Stop-all and bank-unload commands release the original ownership/status fields. Python execution of the original IOP instructions supplies the reference; 17 IOP checks and native runtime checks pass. Muted session 0735648f13ed46a58f292326ce32944f captured 359 fresh samples, nine completed voices, zero busy sound batches, no unhandled RPCs, no missing targets and no fatal runtime errors. Character-select bank 2 unloads after choosing difficulty.



**Opening cinematic now plays after difficulty selection.** RNA teardown now returns cancelled buffers after closing the stream. The original no-skip 345-second test completed opening.sfd naturally and reached Loading. Eighteen IOP checks and 30 native runtime fixtures passed that correction. The latest 420-second run also completed opening playback without skipping and reached scene drawing after loading; scene rendering is still incorrect.



**Loading now reaches Liu Kang's HUD and scene drawing, but the scene is badly distorted.** Shared COP2 wait and scratchpad-DMA corrections are followed by a VSUB fix: vector register VF0 stays constant, while required MAC/STATUS flags are updated. New threads and interrupt/callback contexts now initialize VF0 correctly too. The earlier negative physics bound and allocator/audio pointer failures did not recur in the latest sampled run. Six more original retail callbacks were restored; the supplemental map now contains 847 rows including the boot entry. No game bound, readiness value or cutscene result is forced.



The last full-opening 420-second muted live test followed Normal difficulty and completed opening.sfd without skipping, then reached Liu Kang's HUD and scene drawing. It collected 1,514 fresh inspector snapshots with no missing callbacks, fatal-runtime-error lines, sampled VF0 anomaly or negative broad-phase bound. The movie decoder had served 6,792 pictures before shutdown around 303 seconds. Scene geometry remains badly distorted against white. **Goro's Lair title, falling characters, Raiden dialogue and playable controls remain unverified.**



Four verified SDK math mismatches were corrected: XYZ-only normalization and dot product, direction-row normalization, and the single-vector projection mode. The focused native check passes 39 cases; these fixes are not proven to resolve the scene. Existing validation includes 1,344 VSUB cases, 224 generated VSUB variants, thread/interrupt regressions and the inspector check. New bounded captures contain 4,096 submitted triangles and eight VU1 program starts. Next priority is comparing original view-transform data with actual VU geometry output. See docs/WORKFLOW.md for exact build/session evidence. Audible character-name quality and reported screeching remain unverified; automated diagnostics stay muted.



**Latest rendering progress: texture uploads no longer become VU commands.** Python reproduced the bad DIRECT boundary; the portable C++ correction passes five native pixel/control tests. A 180-second live run produced 474 fresh inspector samples with no XGKICK failures, missing function targets, or recorded runtime errors. Textured scene geometry and Liu Kang's HUD now render. Large white areas, striping and distorted geometry remain. Goro's Lair introduction, controls in the level and gameplay are not yet verified.



Movie clipping was corrected by restoring five original display setup functions. Menu shaking was corrected with complementary interlaced-field presentation; warning text matched across 31 consecutive frame comparisons, and menu headings remained stationary with animation continuing. Motion-adaptive deinterlacing is deferred.



[Xbox, DualSense and DualShock 4 mappings](docs/CONTROLLERS.md) are installed, including the imported PCSX2 Pad1 profile. Physical DualSense Cross reached the title screen; other physical controls/models still require confirmation.



See [sound and transition evidence](docs/SFSV-RPC.md), [runtime inspector](docs/INSPECTOR.md), [workflow](docs/WORKFLOW.md), and [prototype symbols](docs/PROTOTYPE.md). logs/cri-pcm-probe.json stores the movie and next-load checkpoint; logs/sndf-voice-probe.json stores the sound checkpoint; logs/inspector/report.json and frame.png are overwritten by the next diagnostic run.

