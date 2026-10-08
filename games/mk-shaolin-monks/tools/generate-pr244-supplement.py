"""Import selected guest entries regenerated from retained PR #244 boundary metadata."""

from pathlib import Path

import re

r=Path(__file__).resolve().parents[1]

pairs=re.findall(r'g_ps2RecompiledFunctionTable\[\d+\] = (\w+); // (0x[0-9a-f]+)',(r/'runtime/pr244-output/register_functions.cpp').read_text())

table={int(a,16):name for name,a in pairs}

targets=[int(line.strip(),16) for line in (r/'config/pr244-entry-points.txt').read_text().splitlines() if line.strip() and not line.startswith('#')]

names=sorted({table[a] for a in targets})

lines=['// Locally regenerated guest entries using retained PR244 boundary metadata.', '#include "game_overrides.h"','#include "pr244-selected/load-compat.h"']

for name in names:

    source=(r/'runtime/pr244-output'/f'{name}.cpp').read_text()

    source=re.sub(r'\b'+re.escape(name)+r'\b','pr244_'+name,source)

    (r/'runtime/pr244-selected'/f'{name}.inc').write_text(source)

    lines.append(f'#include "pr244-selected/{name}.inc"')

lines.append('namespace { void installPr244Entries(PS2Runtime& runtime) {')

for name,address in pairs:

    if name in names:lines.append(f'runtime.replaceFunction({address}u, pr244_{name});')

lines+=['} }','PS2_REGISTER_GAME_OVERRIDE("Shaolin Monks PR244 entries", "SLUS_210.87", 0x11c070u, 0x1a37a67cu, installPr244Entries)']

(r/'runtime/pr244-hooks.inc').write_text('\n'.join(lines)+'\n')

print(f'Imported {len(names)} automatically bounded functions for {len(targets)} requested entries')

