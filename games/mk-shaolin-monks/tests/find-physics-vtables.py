"""Discover retail physics vtable gaps near three live first-stage objects.

Candidate discovery only. Review boundaries in Ghidra before adding entries.
Existing translations and game memory are never modified by this probe.
"""
from pathlib import Path
import csv, hashlib, json, re, struct

GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parents[1]
SHA = 'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'


def main():
    blob = (ROOT / 'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
    assert hashlib.sha256(blob).hexdigest() == SHA
    ph = struct.unpack_from('<I', blob, 28)[0]
    stride, count = struct.unpack_from('<HH', blob, 42)
    segments = [struct.unpack_from('<8I', blob, ph + i * stride) for i in range(count)]

    def word(address):
        for kind, offset, base, _, size, *_ in segments:
            if kind == 1 and base <= address and address + 4 <= base + size:
                return struct.unpack_from('<I', blob, offset + address - base)[0]
        raise ValueError(hex(address))

    main_source = (GAME / 'runtime/output/register_functions.cpp').read_text()
    known = {int(a, 16) for a in re.findall(r'// (0x[0-9a-f]+)', main_source)}
    with (GAME / 'config/kernel-functions.csv').open() as source:
        known.update(int(row['start'], 16) for row in csv.DictReader(source))
    tables = []
    for address in range(0x5669d0, 0x567c00, 8):
        if [word(address + i) for i in (0, 4, 8)] != [0, 0, 0]:
            continue
        methods = []
        for offset in range(12, 512, 8):
            value = word(address + offset)
            if value == 0:
                break
            if not (0x100000 <= value < 0x4b2000 and value % 4 == 0 and word(address + offset + 4) == 0):
                methods = []
                break
            methods.append(value)
        else:
            methods = []  # Reject unterminated candidates.
        if len(methods) >= 3:
            tables.append({'address': hex(address), 'methods': [hex(v) for v in methods],
                           'missing': [hex(v) for v in methods if v not in known]})
    anchors = {0x566f90, 0x567868, 0x567a80}
    assert anchors <= {int(t['address'], 16) for t in tables}, 'Live-table layout no longer matches'
    missing = sorted({int(v, 16) for t in tables for v in t['missing']})
    report = {'elf_sha256': SHA, 'live_table_anchors': sorted(map(hex, anchors)),
              'scope': 'Bounded pointer-layout candidates; not proof every method executes',
              'tables': tables, 'missing': [hex(v) for v in missing]}
    (GAME / 'logs/physics-vtables.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(tables)} table candidates; {len(missing)} missing entries')
    print(' '.join(report['missing']))


if __name__ == '__main__':
    main()
