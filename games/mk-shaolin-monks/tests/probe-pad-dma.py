"""Replay retail 2125f4..21260c: direct reads of the libpad DMA status bytes."""
import json,pathlib,runpy,struct
GAME=pathlib.Path(__file__).resolve().parents[1]
S=runpy.run_path(str(GAME/'tests/transfer-prototype-symbols.py'))
read=S['image'](S['ROOT']/'MortalKombatShaolinMonks/SLUS_210.87',S['HASHES']['retail'])
def decode(status):
    ram=bytearray(0x300);ram[0x102:0x104]=status
    struct.pack_into('<H',ram,0x200,65535)
    r=[0]*32;r[16]=0x200;r[17]=0x100
    for pc in range(0x2125f4,0x212610,4):
        w=int.from_bytes(read(pc,4),'little');op=w>>26;rs=w>>21&31;rt=w>>16&31;rd=w>>11&31;sa=w>>6&31;fn=w&63;imm=w&65535
        if op==37:r[rt]=int.from_bytes(ram[r[rs]+imm:r[rs]+imm+2],'little')
        elif op==36:r[rt]=ram[r[rs]+imm]
        elif op==0 and fn==0:r[rd]=(r[rt]<<sa)&0xffffffff
        elif op==0 and fn==39:r[rd]=~(r[rs]|r[rt])&0xffffffff
        elif op==0 and fn==36:r[rd]=r[rs]&r[rt]
        elif op==41:struct.pack_into('<H',ram,r[rs]+imm,r[rt]&65535)
        else:raise ValueError(hex(w))
    return struct.unpack_from('<H',ram,0x204)[0]
cases=[]
for name,status,expected in [('stale_dma',b'\0\0',65535),('neutral',b'\xff\xff',0),('cross',b'\xff\xbf',64),('start',b'\xf7\xff',2048)]:
    got=decode(status);assert got==expected
    cases.append(dict(name=name,wire=status.hex(),retail_buttons=hex(got)))
run=json.loads((GAME/'logs/inspector/report.json').read_text())
observed=[x for x in run['input_samples'] if x['pads'][0]['buttons_active_low']==49151]
assert observed
live=[]
for sample in observed:
    watches={w['name']:bytes.fromhex(w['bytes']) for w in sample['watches']}
    status=watches['input_shell'][2:4]
    assert status in (b'\0\0',b'\xff\xbf')
    assert int.from_bytes(watches['input_shell_state'][:4],'little')==64
    live.append(dict(elapsed=sample['elapsed'],dma_status=status.hex(),retail_buttons=decode(status)))
(GAME/'logs/pad-dma-probe.json').write_text(json.dumps(dict(elf_sha256=S['HASHES']['retail'],
    session=run['session'],cases=cases,live=live,passed=True),indent=2)+'\n')
print('PASS: original retail sees all buttons from stale DMA, Cross from ff bf, neutral from ff ff')
