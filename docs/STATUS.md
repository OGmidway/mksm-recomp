# Current status and next work

This is an engineering handoff, not a playable release announcement. The last confirmed local tests were recorded during the September 2026 investigation; the public handoff was assembled on October 8, 2026.

## Confirmed local progress

- Retail NTSC boot, Midway movie, autosave warning, title/menu, character selection and Normal/Hard difficulty dialog.
- Opening movie completed naturally in a 420-second run (1,514 fresh inspector samples, 6,792 decoded pictures), followed by Loading and scene drawing.
- Main-menu shaking and movie display offset received targeted fixes. Audio quality remains unverified.
- A VIF DIRECT parsing bug treated texture bytes as commands, launched an unintended VU program and corrupted geometry data. Python reproduced it; the C++ correction and split-payload continuation pass nine native cases.
- Later live tests reached textured cave geometry and Liu Kang's HUD without the prior XGKICK failure. The scene remains severely distorted/overexposed.
- Host stick extremes and neutral reached guest pad reads. Camera movement was observed, but actor control and gameplay are not established by that evidence.

## Highest-priority blocker: GS feedback/bloom

Pixel traces show repeated additive sprites reading texture base 4480 while writing framebuffer base 140: the same VRAM location. The sampled colors rise toward 255 before compositing. Fog is off for those sprites and COLCLAMP is enabled. This identifies the failing rendering stage, not a proven final fix.

The CPU backend reads textures directly from current VRAM; `TextureFlush()` has no cache to invalidate. PCSX2 documents page-buffered texture feedback in `GSState::HandleAutoFlush`, and its game database enables auto-flush for SLUS-21087 to correct bloom intensity. These are references, not code to copy blindly:

- [PCSX2 GS state handling](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSState.cpp)
- [PCSX2 game database](https://github.com/PCSX2/pcsx2/blob/master/bin/resources/GameIndex.yaml)

A bounded feedback capture now records one sprite pass, flush serials and VRAM before/after.

The real captured session is `a49cb286f53f4452bb2c0c2d73cadae1`, runner SHA `fc3a4c89e50385a1416118b689a68bdbadbdbc349b8195b50c47354cfbf42af7`, with 840 sprite draws and two bounded 4 MiB VRAM snapshots. Captures stay local. `tests/replay-feedback.py` compares coherent reads and cache hypotheses. The 2026-10-09 coherent replay matched all 1,048,576 native VRAM words after 840 draws and 855,296 pixel writes. Three cache hypotheses changed 8,363 to 8,970 words, but reduced whole-VRAM white counts only slightly. Main framebuffers already contained many white pixels before this late pass; a fresh earliest-pass capture now shows cave geometry with zero pure-white pixels before bloom and 588 afterward. Its coherent replay also matches exactly. A 145-second live run yielded 417 fresh samples without reported runtime/XGKICK errors, but the final image remained badly overexposed. Next investigate the final composite and clearing across early frames; no cache hypothesis has been promoted to C++. Do not turn an unvalidated cache hypothesis into a production fix.

Other useful leads: the sprite renderer truncates fractional XYOFFSET/UV components, and prototype-map matches identify candidate post-processing routines. Candidate addresses and their limitations are in `games/mk-shaolin-monks/ghidra/bloom-candidates.json`.

## Completion gates

1. Verify the original Goro's Lair title, falling Liu Kang/Kung Lao and Raiden dialogue.
2. Verify correct 3D rendering and controllable gameplay, including movement, combat and progression.
3. Resolve audio/runtime faults that block those sequences.
4. Validate supported host platforms with live evidence. Portable source alone is insufficient.

## Public handoff validation

Publication checks on 2026-10-08 regenerated the main retail translations (8,861 functions), supplemental map and seven selected PR244 entries using the existing local compiler. Generated hooks and outputs remain ignored. Python, JSON and TOML are checked by the indexed-source audit. An optional GitHub CI template builds and runs focused game-data-free VIF/VU checks; it is not active because the publishing token lacks workflow permission. The public checkout freshly built its independent IOP suite with MSVC 19.51 and passed all 16 game-data-free tests. The retail bank/ADPCM test remains opt-in and requires local fixtures. All 1,291 formula-generated audio lookup values match the local reference. A full clean-clone game build and live gameplay have not yet been revalidated. Historical captures referenced in the detailed workflow are intentionally absent from Git.

The capture now includes final compositing across framebuffer changes. A second 145-second run (413 fresh samples) validated 1,120 draws and 1,142,016 writes with zero replay mismatches. The first main-frame composite remained below pure white; later saturation still needs tracing across frame clearing, depth and subsequent composites. See the latest workflow entry.
