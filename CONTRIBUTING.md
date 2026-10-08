# Contributing

Open an issue describing the specific blocker you want to investigate, or comment on an existing task so work does not overlap. Fork the repository, create a focused branch and submit a pull request to `main`. Public contributions use normal GitHub PR review; write access to the repository is not required.

## Working method

1. Reproduce the problem with the supported retail ELF. Record the commit, toolchain, reproduction steps and inspector session.
2. Use Python for a focused experiment when it can establish expected behavior quickly.
3. Implement supported behavior in portable C++. Prefer fixing shared PS2 behavior when evidence warrants it.
4. Run relevant focused tests and the affected live game sequence. Record what actually appeared and what remains unverified.
5. Update existing findings instead of creating duplicate reports. Keep generated files and raw captures out of the PR.

Distinguish verified retail symbols from prototype candidates. The prototype map does not match its executable exactly and has no universal relocation offset. Do not import names based on a short prefix alone.

Avoid forcing loader readiness, skipping game functions, suppressing bloom, or faking successful RPC results to make a screenshot look better. A diagnostic switch must be opt-in and clearly identified. Preserve the normal retail path.

## Useful contributions

- Correct GS texture-feedback/page-cache and bloom behavior, with a reproduction and reference.
- Resolve distorted first-stage geometry and verify the original Goro's Lair introduction.
- Confirm actor movement, combat, camera behavior and stage progression with actual input.
- Test Xbox, DualSense and DualShock controls; document OS/backend details.
- Fix blocking IOP/RPC/audio behavior from observed packet layouts and completion semantics.
- Validate Linux/macOS builds without weakening the retail behavior or Windows checks.

## Submission checklist

Explain the problem, evidence, fix and test results. Add a focused regression when it verifies meaningful behavior. Include small screenshots only when you can share them appropriately; never attach disc images, extracted assets, ELF/IRX files, proprietary SDKs, generated retail translations, raw memory/audio captures or Ghidra projects.

Run `python tools/audit-publication.py` before submitting. Reports should omit account tokens, personal paths, device identifiers and unrelated desktop content. Contributions are submitted under the repository's GPL-3.0 license; retain existing notices and attribution.
