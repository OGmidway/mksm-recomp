"""Run the retail CRI resume/wakeup chain with two proposed syscall results.

Original hash-checked MIPS executes with delay slots; only BIOS calls and the
unrelated second-worker mode query are modeled. This is not a runtime test.
"""
import importlib.util
import json
import pathlib
import struct

GAME = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('symbols', GAME/'tests/transfer-prototype-symbols.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
MASK = (1 << 64) - 1

def signed(x, bits):
    x &= (1 << bits) - 1
    return x - (1 << bits) if x >> (bits - 1) else x

def execute(read, resume_result, initial_status=12, entry=0x4154f8, wake_result=5):
    ram = bytearray(0x2000000)
    tid, status = 5, initial_status
    struct.pack_into('<I', ram, 0x5341cc, tid)
    struct.pack_into('<I', ram, 0x5341c8, tid)
    struct.pack_into('<I', ram, 0x53417c, 1)
    r = [0]*32
    r[29], r[31] = 0x1e00000, 0x80000
    pc, pending, calls = entry, None, []
    for count in range(600):
        if pc == 0x80000:
            assert r[29] == 0x1e00000
            return {'resume_result': resume_result, 'initial_status': initial_status,
                    'final_status': status, 'calls': calls, 'entry':hex(entry),
                    'wake_result':wake_result,'pending_main':struct.unpack_from('<I',ram,0x53417c)[0]}
        if pc in (0x47fd60, 0x47fdf0, 0x47fd90, 0x414d08):
            assert pending is None
            if pc == 0x414d08:
                r[2] = 0  # Exclude the independent, mode-gated second worker.
            else:
                assert r[4] == tid
                if pc == 0x47fd60:
                    struct.pack_into('<I', ram, r[5], status)
                    r[2] = tid
                elif pc == 0x47fdf0:
                    calls.append('ResumeThread')
                    if resume_result >= 0:
                        status = 4 if status == 12 else 2
                    r[2] = resume_result & MASK
                else:
                    calls.append('WakeupThread')
                    assert status == 4
                    status, r[2] = 2, wake_result
            pc = r[31]
            continue
        assert (0x4154f8 <= pc < 0x415600 or 0x414988 <= pc < 0x4149d8
                or 0x4149f0 <= pc < 0x414a58), hex(pc)
        w = struct.unpack('<I', read(pc, 4))[0]
        op, rs, rt, rd, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w & 63
        imm = w & 65535
        si = signed(imm, 16)
        old, pending, nxt = pending, None, pc + 4
        if w == 0: pass
        elif op == 15: r[rt] = signed(imm << 16, 32)
        elif op == 9: r[rt] = signed(r[rs] + si, 32)
        elif op == 0 and fn == 45: r[rd] = r[rs] + r[rt]
        elif op in (35, 55):
            size = 4 if op == 35 else 8
            a = r[rs] + si
            assert 0 <= a <= len(ram) - size
            r[rt] = int.from_bytes(ram[a:a+size], 'little', signed=op == 35)
        elif op in (43,63):
            a = r[rs] + si
            size=4 if op==43 else 8
            assert 0 <= a <= len(ram) - size
            ram[a:a+size]=(r[rt]&((1<<(size*8))-1)).to_bytes(size,'little')
        elif op in (4, 5, 20, 21):
            taken = (r[rs] == r[rt]) == (op in (4,20))
            if taken: pending = pc + 4 + si*4
            elif op in (20,21): nxt = pc + 8
        elif op in (2, 3):
            if op == 3: r[31] = pc + 8
            pending = ((pc + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2)
        elif op == 0 and fn == 8: pending = r[rs] & 0xffffffff
        else: raise ValueError(f'Unsupported {w:08x} at {pc:x}')
        r = [x & MASK for x in r]
        r[0] = 0
        pc = old if old is not None else nxt
    raise ValueError('Instruction limit exceeded')

def main():
    read = s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87', s.HASHES['retail'])
    cases = [execute(read, result) for result in (0, 5, -416)]
    assert cases[0]['calls'] == ['ResumeThread'] and cases[0]['final_status'] == 4
    assert cases[1]['calls'] == ['ResumeThread', 'WakeupThread'] and cases[1]['final_status'] == 2
    assert cases[2]['calls'] == ['ResumeThread'] and cases[2]['final_status'] == 12
    cases.append(execute(read, 5, 8))
    assert cases[-1]['calls'] == ['ResumeThread'] and cases[-1]['final_status'] == 2
    cases.extend(execute(read,5,4,0x415580,result) for result in (0,5))
    assert cases[-2]['pending_main']==1 and cases[-1]['pending_main']==0
    report = {'sha256': s.HASHES['retail'], 'passed': True, 'entry': '0x4154f8',
              'scope': 'Original retail control flow with modeled BIOS results, not live scheduling',
              'cases': cases}
    (GAME/'logs/cri-resume-probe.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS: retail CRI resume/wake and main-wakeup flag require successful syscall thread IDs')

if __name__ == '__main__': main()
