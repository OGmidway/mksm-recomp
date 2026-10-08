"""Reference COP2 branch condition for the observed retail loading wait."""
from pathlib import Path
import hashlib, json, struct
GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parents[1]
blob = (ROOT / 'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
assert hashlib.sha256(blob).hexdigest() == 'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
ph = struct.unpack_from('<I', blob, 28)[0]
stride, count = struct.unpack_from('<HH', blob, 42)
for i in range(count):
    kind, offset, base, _, size, *_ = struct.unpack_from('<8I', blob, ph + i * stride)
    if kind == 1 and base <= 0x24e780 < base + size:
        instruction = struct.unpack_from('<I', blob, offset + 0x24e780 - base)[0]
        break
else:
    raise AssertionError('Retail branch missing')
assert instruction == 0x4901fffb
assert 0x24e784 + (-5 * 4) == 0x24e770
cases = []
for arithmetic_status in (0, 1, 0xffff):
    for vpu_stat in (0, 1, 0x100, 0x101, 0x200, 0x400, 0x700):
        for variant in range(4):
            busy = bool(vpu_stat & 0x100)
            taken = busy if variant & 1 else not busy
            wrong_busy = bool(arithmetic_status & 1)
            wrong = wrong_busy if variant & 1 else not wrong_busy
            cases.append(dict(arithmetic_status=arithmetic_status, vpu_stat=vpu_stat,
                              variant=variant, taken=taken, old_taken=wrong,
                              delay_executes=(taken or variant < 2)))
assert any(c['taken'] != c['old_taken'] for c in cases)
assert next(c for c in cases if c['arithmetic_status'] == 1 and c['vpu_stat'] == 0 and c['variant'] == 1)['taken'] is False
report = dict(retail_pc='0x24e780', instruction=hex(instruction), target='0x24e770',
              reference='https://github.com/PCSX2/pcsx2/blob/master/pcsx2/COP2.cpp',
              condition='VPU_STAT bit 8 (VU1 running), independent of arithmetic STATUS',
              cases=cases, false_loop_case=dict(arithmetic_status=1, vpu_stat=0))
(GAME / 'logs/vu-branch-probe.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'PASS: {len(cases)} reference branch cases; old arithmetic-zero condition reproduces false busy loop')
