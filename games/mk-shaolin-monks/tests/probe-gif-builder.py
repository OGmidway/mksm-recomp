"""Replay retail AddGsAD/Terminate; demonstrate mixed-HLE DMA length corruption."""
import importlib.util,json,struct
from pathlib import Path
GAME=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('symbols',GAME/'tests/transfer-prototype-symbols.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
read=s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',s.HASHES['retail'])
MASK=(1<<64)-1
def sx(v):
    v&=0xffffffff
    return v-(1<<32) if v>>31 else v
def execute(pc,ram,args):
    r=[0]*32;r[4:4+len(args)]=args;r[31]=0x80000;pending=None
    for _ in range(500):
        if pc==0x80000:return r[2]
        w=int.from_bytes(read(pc,4),'little');op=w>>26;rs=w>>21&31;rt=w>>16&31;rd=w>>11&31;sa=w>>6&31;fn=w&63
        imm=w&65535;si=imm-65536 if imm>>15 else imm;old=pending;pending=None
        if w==0:pass
        elif op==35:r[rt]=sx(int.from_bytes(ram[(r[rs]+si)&0x1fffffff:((r[rs]+si)&0x1fffffff)+4],'little'))
        elif op in (43,63):
            n=4 if op==43 else 8;a=(r[rs]+si)&0x1fffffff
            assert 0<=a<=len(ram)-n
            ram[a:a+n]=(r[rt]&((1<<(8*n))-1)).to_bytes(n,'little')
        elif op==9:r[rt]=sx(r[rs]+si)
        elif op==12:r[rt]=r[rs]&imm
        elif op==13:r[rt]=r[rs]|imm
        elif op==15:r[rt]=sx(imm<<16)
        elif op in (4,5):
            if (r[rs]==r[rt])==(op==4):pending=pc+4+si*4
        elif op==0 and fn==8:pending=r[rs]
        elif op==0 and fn==3:r[rd]=sx(r[rt])>>sa
        elif op==0 and fn==2:r[rd]=sx((r[rt]&0xffffffff)>>sa)
        elif op==0 and fn==36:r[rd]=r[rs]&r[rt]
        elif op==0 and fn==37:r[rd]=r[rs]|r[rt]
        elif op==0 and fn==60:r[rd]=r[rt]<<(32+sa)
        elif op==0 and fn==62:r[rd]=r[rt]>>(32+sa)
        elif op==0 and fn in (33,35):r[rd]=sx(r[rs]+(r[rt] if fn==33 else -r[rt]))
        elif op==0 and fn==45:r[rd]=r[rs]+r[rt]
        else:raise ValueError(f'unsupported {w:08x} at {pc:x}')
        r=[x&MASK for x in r];r[0]=0;pc=old if old is not None else pc+4
    raise ValueError('instruction limit')

cases=[]
for count in (1,4,16):
    ram=bytearray(0x40000);state,tag=0x20000,0x21000
    struct.pack_into('<4I',ram,state,tag+16,tag,tag,0)
    struct.pack_into('<I',ram,tag,0x70000000)
    for i in range(count):execute(0x384d30,ram,[state,0x50+i,0x1122334455660000+i])
    assert struct.unpack_from('<I',ram,tag)[0]==0x70000000
    before=ram[:]
    execute(0x3849a8,ram,[state]);expected=struct.unpack_from('<I',ram,tag)[0]&65535
    assert expected==count
    # Current HLE updates pending QWC before the original finalizer adds it.
    struct.pack_into('<I',before,tag,0x70000000|count)
    execute(0x3849a8,before,[state]);mixed=struct.unpack_from('<I',before,tag)[0]&65535
    assert mixed==count*2
    cases.append(dict(records=count,retail_qwc=expected,mixed_hle_qwc=mixed))
spr=[]
for offset in (0,0x1230,0x3ff0):
    dma=execute(0x3841d0,ram,[0x70000000+offset])&0xffffffff
    assert dma==0x80000000+offset
    spr.append(dict(cpu=hex(0x70000000+offset),dma=hex(dma),old_hle=hex(dma&0x1fffffff)))
(GAME/'logs/gif-builder-probe.json').write_text(json.dumps(dict(elf_sha256=s.HASHES['retail'],passed=True,cases=cases,scratchpad_addresses=spr),indent=2)+'\n')
print('PASS: original packet builders defer DMA count; mixed HLE doubles all 3 tested chains')
print('PASS: original SDK encodes scratchpad with bit 31; old HLE sends it to main RAM')
