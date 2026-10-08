"""Explain the observed retail broad-phase loop using original opcodes and a saved live snapshot."""
from pathlib import Path
import hashlib,json,struct
GAME=Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
RUN=GAME/'logs/first-stage-relinked'
blob=(ROOT/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
digest=hashlib.sha256(blob).hexdigest()
assert digest=='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
ph=struct.unpack_from('<I',blob,28)[0]
stride,count=struct.unpack_from('<HH',blob,42)
segments=[struct.unpack_from('<8I',blob,ph+i*stride) for i in range(count)]
def word(address):
    for kind,offset,base,_,size,*_ in segments:
        if kind==1 and base<=address and address+4<=base+size:
            return struct.unpack_from('<I',blob,offset+address-base)[0]
    raise ValueError(hex(address))
expected={0x3a0380:0x2631fffc,0x3a0394:0x8a250003,0x3a0398:0x9a250000,
          0x3a039c:0xaa250007,0x3a03a0:0xba250004,0x3a040c:0x9624fffc,
          0x3a0410:0x02e4102a,0x3a0414:0x5440ffda,0x3a01e4:0x8d170018}
for address,value in expected.items(): assert word(address)==value,hex(address)
assert 0x3a0418-38*4==0x3a0380
assert struct.unpack('<f',struct.pack('<I',word(0x5945c0)))[0]==32767.0
run=json.loads((RUN/'report.json').read_text())
snapshot=json.loads((RUN/'inspector.json').read_text())
assert run['elf_sha256']==digest and run['session']==snapshot['session'] and run['pid']==snapshot['pid']
assert snapshot['cpu']['pc']==0x3a0380,'Capture has progressed; update the investigation before interpreting this fixture'
raw=snapshot['cpu']['gpr_u64'][23]
bound=raw-(1<<64) if raw>>63 else raw
assert bound<0,'Negative-bound failure no longer present'
# LHU yields 0..65535; signed SLT(s7,a0), then BNEL, repeats for every value.
assert all(bound<entry for entry in range(65536))
# The unaligned load/store pair copies one entry to the next four-byte slot.
ram=bytearray(range(32));original=ram[:];cursor=16
for _ in range(3):
    cursor-=4;ram[cursor+4:cursor+8]=ram[cursor:cursor+4]
assert ram[8:20]==original[4:16]
report=dict(session=run['session'],runner_sha256=run['runner_sha256'],elf_sha256=digest,
    sequence=snapshot['sequence'],pc='0x3a0380',bound_signed=bound,bound_hex=hex(raw),
    cursor=hex(snapshot['cpu']['gpr_u64'][17]),node=hex(snapshot['cpu']['gpr_u64'][16]),
    exhaustive_entry_cases=65536,stop_possible=False,entry_shift_bytes=4,
    bound_load='0x3a01e4: LW s7,0x18(t0)',
    interpretation='The saved negative bound prevents termination and permits backward shifts outside the endpoint array. The source of the negative bound is not yet proven.',
    opcodes={hex(a):hex(v) for a,v in expected.items()})
(GAME/'logs/broadphase-bound-probe.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'PASS: retail opcodes, four-byte shifts and all 65,536 entry comparisons; live bound={bound}')
