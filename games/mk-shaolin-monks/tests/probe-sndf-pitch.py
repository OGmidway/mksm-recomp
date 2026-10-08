"""Check the retail SNDF pitch expression and execute its IOP pitch conversion."""
import hashlib, importlib.util, json, math, struct
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sndf', GAME/'tests/probe-sndf-init.py')
sndf = importlib.util.module_from_spec(spec); spec.loader.exec_module(sndf)

def main():
    elf = (GAME.parents[1]/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    assert digest == 'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
    def word(address): return struct.unpack_from('<I', elf, address-0xff000)[0]
    # Real call site: convert float to double, pass v0 in a0 to log, then
    # consume v0 again. FPR f12/f0 are not this routine's double ABI.
    assert word(0x4629b0) == 0x0c080e0c
    assert word(0x4629b8) == 0x0c11c2ce
    assert word(0x4629bc) == 0x0040202d
    assert word(0x4629cc) == 0x0040202d
    ln2, cents = struct.unpack_from('<2d', elf, 0x5ae1f8-0xff000)
    assert ln2 == math.log(2) and cents == 1200
    ratios = (0.5, 1.0, 2.0, 4.0)
    cases = [{'ratio': r, 'pitch_base': math.trunc(math.log(r)/ln2*cents)} for r in ratios]
    assert [c['pitch_base'] for c in cases] == [-1200, 0, 1200, 2400]
    # The old float-only stub leaves v0 containing the input double 1.0.
    stale = math.trunc(1.0/ln2*cents)
    assert stale == 0x6c3
    cpu = sndf.Sndf(); cpu.initialize()
    pitches = []
    for base in (stale, 0):
        cpu.put(sndf.DRIVER+0x24, base)
        pitch = cpu.call(0x56f0, [(-1347)&0xffffffff])
        pitches.append({'pitch_base': base, 'first_voice_pitch': pitch})
    assert [p['first_voice_pitch'] for p in pitches] == [0x2b4, 0x759]
    result = {'retail_sha256': digest, 'scope': 'ELF call-site/constant checks, Python math reference, original IOP pitch instructions; no playback claim',
              'cases': cases, 'startup_stale_v0_pitch_base': stale, 'startup_expected_pitch_base': 0,
              'first_voice': pitches, 'passed': True}
    (GAME/'logs/sndf-pitch-probe.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: retail double ABI, startup pitch base 0 (old stub 1731), original IOP pitches 0x2b4/0x759')

if __name__ == '__main__': main()
