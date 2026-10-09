# Current status and next work

This is an engineering handoff, not a playable release announcement. The latest local runtime validation was recorded on October 9, 2026; the public handoff was assembled on October 8.

## Confirmed local progress

- Retail NTSC boot, Midway movie, autosave warning, title/menu, character selection and Normal/Hard difficulty dialog.
- Opening movie completed naturally in a 420-second run (1,514 fresh inspector samples, 6,792 decoded pictures), followed by Loading and scene drawing.
- Main-menu shaking and movie display offset received targeted fixes. Audio quality remains unverified.
- A VIF DIRECT parsing bug treated texture bytes as commands, launched an unintended VU program and corrupted geometry data. Python reproduced it; the C++ correction and split-payload continuation pass nine native cases.
- Later live tests reached textured cave geometry and Liu Kang's HUD without the prior XGKICK failure. The scene remains severely distorted. The October 9 clear-packet fix removes the observed white accumulation.
- Host stick extremes and neutral reached guest pad reads. Camera movement was observed, but actor control and gameplay are not established by that evidence.

## Highest-priority blocker: scene geometry and camera framing

The latest 145-second run reaches textured scene drawing with no white accumulation, but the image is mostly dark with incorrectly framed/distorted surfaces. The Goro's Lair title, falling characters, dialogue and controllable gameplay still need verification. Trace retail camera/projection and emitted geometry next; do not mistake active drawing for playable gameplay.

## Fixed: stale depth and white accumulation

`sceGsPutDrawEnv` receives a GIF-tag pointer. Its old HLE treated the tag as a register and applied only eight entries, dropping the subsequent clear draws. The corrected implementation submits NLOOP+1 quadwords through normal GIF DMA. A related normal-DMA fix preserves MADR bit 31 as the scratchpad selector.

Python executed original retail instructions for 40 packet-length/address/busy-state cases before implementation. A native color/depth clear regression failed before the fix and passes afterward for RAM, uncached RAM and scratchpad packets. The existing native check executable passes.

Post-fix session `aaef99988bf343c2b7162673047bc1ab` produced 408 fresh inspector samples without reported runtime/XGKICK errors. Its bounded trace contains 344 depth-zero writes and no depth rejections at the two sampled pixels; the final image contains no white or near-white pixels. This run used the diagnostic intro skip and does not prove natural cutscene completion or gameplay.

Independent Python bloom replay still matches all native VRAM words after 1,120 draws and 1,142,016 writes. No speculative texture-cache or bloom-disable workaround is enabled. Historical cache experiments and exact capture identities remain in the [workflow](../games/mk-shaolin-monks/docs/WORKFLOW.md); candidate depth/post-processing symbols remain in `games/mk-shaolin-monks/ghidra/bloom-candidates.json`.

## Completion gates

1. Verify the original Goro's Lair title, falling Liu Kang/Kung Lao and Raiden dialogue.
2. Verify correct 3D rendering and controllable gameplay, including movement, combat and progression.
3. Resolve audio/runtime faults that block those sequences.
4. Validate supported host platforms with live evidence. Portable source alone is insufficient.

## Public handoff validation

Publication checks on 2026-10-08 regenerated the main retail translations (8,861 functions), supplemental map and seven selected PR244 entries using the existing local compiler. Generated hooks and outputs remain ignored. Python, JSON and TOML are checked by the indexed-source audit. An optional GitHub CI template builds and runs focused game-data-free VIF/VU checks; it is not active because the publishing token lacks workflow permission. The public checkout freshly built its independent IOP suite with MSVC 19.51 and passed all 16 game-data-free tests. The retail bank/ADPCM test remains opt-in and requires local fixtures. All 1,291 formula-generated audio lookup values match the local reference. A full clean-clone game build and live gameplay have not yet been revalidated. Historical captures referenced in the detailed workflow are intentionally absent from Git.
