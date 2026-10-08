# PR #244 comparison and Shaolin Monks checkpoint

Paths in this document are relative to the MKSM workspace root unless stated otherwise. Current commands: [WORKFLOW.md](WORKFLOW.md).

Reviewed https://github.com/ran-j/PS2Recomp/pull/244 at downloaded head `78ecbae377a758595049e146b79733bd609fe274` on September 16. The web page was behind the fetched branch head; the comparison uses this exact local revision. The upstream source is isolated at `<repo>\out\upstream-pr244`.

## Relevant changes and measured results

- Callback/function-pointer discovery: the new tool finds 212 of the 213 manually supplied guest entries in our current supplemental map. It still misses `0x422338`; retain that manual entry. The smaller total registration count (288,417 versus 340,148) is not itself a correctness result, because function boundaries and fallback promotion changed.
- Memory hints: the analyzer now resolves fuller constant-producing sequences, and the translator disables the unreliable forced-MMIO substitution. This addresses the class of MMIO error previously worked around in our configuration.
- Unsigned loads: the new generator uses SET_GPR_ZE32 for LBU/LHU/LWU. Its compatible macro is included only for imported PR-generated functions; the main generated code has not been broadly replaced.
- IOP: introduces execution of original IRX code alongside HLE services. This could help the unsupported custom services, but it has not been integrated or validated against Shaolin Monks yet.

## Applied and tested

Built the PR analyzer and recompiler in `out/pr244-tools`, then generated a separate complete translation in `games/mk-shaolin-monks/runtime/pr244-output` using `config/pr244-recompile.toml`. The existing runtime and main generated sources remain the active baseline.

Imported seven automatically discovered guest routines: `0x424FA0`, `0x425058`, `0x425060`, `0x425070`, `0x11BBC8`, `0x212D30`, `0x212D40`. Selection is recorded in `config/pr244-entry-points.txt`; `tools/generate-pr244-supplement.py` imports the selected translations and their resumable entries. `runtime/pr244-hooks.inc` registers them under this game's exact identity. `runtime/loading-trace.cpp` includes that hook file. This removes the observed missing stream and game callbacks without a complete runtime migration.

Release build passed. `tests/verify-archive-boot.py` passed on `logs/boot-20260916-112312.stderr.log`: all four workers create successfully, five threads are present, and the first 59,392 archive bytes match FNV-1a 37d74c15. No missing-target diagnostic appeared during that 15-second run. The harness timeout is not successful game completion.

## Upstream IOP test result

Built the unmodified PR IOP library and tests in `out/pr244-iop-tests`. Three of four suites pass. `ps2_iop_emulator_tests` fails with “Physical sound server bypassed the LotR compatibility stub.” Investigation found the LotR profile registration commented out in `ps2xIOP/src/builtin_profiles.cpp`, while the test at `tests/iop_emulator_tests.cpp:958` still expects its compatibility response. This is a concrete profile/test inconsistency, not proof the entire emulator is unusable. No upstream comment or PR was posted.

## Next blocker

A later archive request still supplies a null buffer. It originates in the asynchronous path at guest call site `0x21244C` (return address `0x212454`): offset `0x10`, byte count `0x30`, destination `0`. These are loaded from request globals `0x651E30`, `0x651E34`, and destination `0x4EF7EC`. The first read used the synchronous path at return address `0x211C50` and succeeded. Trace the producers of those request globals and the buffer allocation; do not replace the error with synthetic success.

Execution still loops around `0x379218/0x379220`. No title screen or gameplay is verified. The draft IOP emulator remains available as a separate next-stage experiment after the EE-side null-buffer cause is resolved.
