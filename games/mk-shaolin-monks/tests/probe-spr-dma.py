"""Focused normal-mode SPR experiment, including the observed retail receive.

Retail SDK setup: ghidra/retail-dma-recv.txt. Hardware cross-check:
https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPR.cpp
This models data/address progression, not DMA timing or chain/MFIFO modes.
"""
import hashlib, json, struct
from pathlib import Path

spr = bytes((i * 37 + 11) & 255 for i in range(16384))
cases = []
for sadr, qwc in ((0, 0x400), (0x3ff0, 2), (0x20, 0x801), (0x3ff0, 0)):
    result = bytes(spr[(sadr + i) % len(spr)] for i in range(qwc * 16))
    assert len(result) == qwc * 16
    if qwc:
        assert result[:16] == spr[sadr:sadr+16]
    if sadr == 0x3ff0 and qwc == 2:
        assert result == spr[-16:] + spr[:16]
    cases.append({'sadr':sadr, 'qwc':qwc, 'bytes':len(result),
                  'final_sadr':(sadr + qwc * 16) & 0x3fff, 'byte_sum':sum(result)})
game = Path(__file__).resolve().parents[1]
elf = (game.parents[1]/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
assert hashlib.sha256(elf).hexdigest() == 'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
def word(a): return struct.unpack_from('<I',elf,a-0xff000)[0]
# Exact retail sequence: mask node address, OR 0x80000000, store MADR, QWC=20.
assert word(0x2da3b4)==0x3c068000 and word(0x2da3c0)==0x00461025
assert word(0x2da3cc)==0xac820000 and word(0x2da3b0)==0x24050014
aliases=[]
for alias in (0,0x80000000,0xa0000000):
    madr=alias|0x80000
    physical=madr&0x1ffffff0
    assert physical==0x80000
    stored=madr&0x7fffffff
    assert not stored&0x80000000 and stored&0x1ffffff0==physical
    aliases.append(dict(madr=hex(madr),stored_madr=hex(stored),physical=hex(physical),old_rejected=bool(madr&0x80000000)))
out = game/'logs/spr-dma-probe.json' 
out.write_text(json.dumps({'cases':cases,'aliases':aliases,'retail_madr_store':'0x2da3cc','reference':'https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Dmac.cpp','passed':True},indent=2)+'\n')
print('PASS: transfer/wrap cases, retail high-bit MADR instruction sequence and three address aliases')
