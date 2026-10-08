"""Audit retail HUD descriptor callbacks; candidates still require Ghidra boundary review."""
from pathlib import Path
import csv, hashlib, json, re, struct
GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parents[1]
blob = (ROOT / 'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
digest = hashlib.sha256(blob).hexdigest()
assert digest == 'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
ph = struct.unpack_from('<I', blob, 28)[0]
stride, count = struct.unpack_from('<HH', blob, 42)
segments = []
for i in range(count):
    kind, offset, base, _, size, *_ = struct.unpack_from('<8I', blob, ph + i * stride)
    if kind == 1: segments.append((offset, base, size))
def words(address):
    for offset, base, size in segments:
        if base <= address and address + 56 <= base + size:
            return struct.unpack_from('<14I', blob, offset + address - base)
    raise ValueError(hex(address))
rows = list(csv.DictReader((GAME / 'config/kernel-functions.csv').open()))
known = {int(r['start'], 0) for r in rows}
registration = (GAME / 'runtime/output/register_functions.cpp').read_text()
known.update(int(x, 16) for x in re.findall(r'; // (0x[0-9a-fA-F]+)', registration))
assert words(0x4c02d8)[12] == 0x181fd0
assert words(0x4c0310)[12] == 0x181538
assert words(0x4c03b8)[12] == 0x1816a8
candidates = {}
# Bounded HUD data region, matched by descriptor scale, RGBA and callback fields.
# Do not scan arbitrary aligned data as function pointers.
for address in range(0x4bc400, 0x4c0ba0, 4):
    w = words(address)
    target = w[12]
    if (0x170000 <= target < 0x190000 and target % 4 == 0
            and w[4] == 0x3f800000 and all(v <= 255 for v in w[6:10])
            and w[0] <= 1024):
        item = candidates.setdefault(target, dict(target=hex(target),
                    already_registered=target in known, descriptors=[]))
        item['descriptors'].append(dict(address=hex(address), words=[hex(v) for v in w]))
report = dict(elf_sha256=digest, descriptor_stride=56, callback_offset=48,
              live_dispatch='0x30fa68 (object + 0x204)',
              note='Pattern candidates, not function-boundary proof. Verify with Ghidra before recovery.',
              candidates=list(candidates.values()))
(GAME / 'logs/hud-callbacks.json').write_text(json.dumps(report, indent=2) + '\n')
missing = [r['target'] for r in candidates.values() if not r['already_registered']]
print(f'{len(candidates)} distinct descriptor callbacks; {len(missing)} missing entries')
print(' '.join(missing))
