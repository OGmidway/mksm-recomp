# MKSM Ghidra setup

Installed Ghidra: `<repo>/Ghidra`, version 12.1.3.
Java: `C:/Program Files/Java/jdk-21.0.11`; Ghidra launch.properties explicitly selects this JDK. Java and javac both verified. This uses Java, not JavaScript.
Emotion Engine Reloaded: release v2.1.37, Ghidra 12.1.3 package, installed to Ghidra/Ghidra/Extensions/ghidra-emotionengine-reloaded. Download SHA-256 matched GitHub's published asset digest: 3af7641174b470bf19ec32b72b3929dbd21ec3f11d66894d2316be044fac1258.

Input ELF: `<repo>/Ghidra/mksm_elf/SLUS_210.87`; SHA-256 b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2. It is identical to the ELF beside the extracted disc assets. Keep the native runner using the extracted disc root so game files remain available.

Language: `r5900:LE:32:default`, compiler `default`. The extension is verified by a successful real ELF import and a hash/language guard script.

Run `../analyze-ghidra.ps1` to create or process the same MKSM_NTSC project and export functions.csv and ghidra.toml here. Existing projects are processed in place; no duplicate projects or build trees are created. Analysis uses at most four CPUs and a 600-second per-file limit. A timed-out analysis is incomplete and needs review. Logs reuse the fixed names under ../logs.

Run `../open-ghidra.ps1` to open the saved project in the GUI. For interactive use of ExportPS2Functions.java, add `<repo>/ps2xRecomp/tools/ghidra` to Script Manager's script directories. The headless launcher already supplies this path and the MKSM pre-analysis script path explicitly.

Exporter: the Java script matching our existing PS2Recomp source, not the transcript's example game. Generated map/config remain analysis inputs until audited; they do not replace the working recompile.toml or verified supplemental callbacks automatically.

Already working: CMake 4.4.3, Git 2.53.0.windows.2, Python 3.14, Visual Studio 2022 MSVC 14.44 (successful native runner/regression builds), PS2 analyzer/recompiler tools. No reinstall was needed for these tools.

The supplied tutorial's suggestions to skip unresolved functions or indiscriminately stub malloc/free are not applied. They would obscure the runtime behavior we are investigating. The extension's DVP overlay and ROM-map warnings require inspection before relying on affected functions.

The PS2Recomp Runtime Debugger is separate from Ghidra. It is compiled into the native runner; its structured live inspector/Python connection is now implemented; see ../INSPECTOR.md.

Validation complete: analysis succeeded (67 seconds reported by Ghidra), exported 12,344 functions and valid TOML, project saved. Corrected settings script recompiled and reran successfully with no ERROR in ghidra-verify-console.log. See setup-report.json.
