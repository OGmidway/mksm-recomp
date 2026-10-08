"""Audit only the retail object vtables observed after screen-fade recovery."""
from pathlib import Path
import csv, hashlib, json, re, struct
GAME=Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
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
# Exact live object-table anchors. No surrounding switch tables are scanned.
anchors=[0x586588,0x592c30,0x576ee8,0x58ce98,0x579a58,0x581958,0x590608,
         0x58ce08,0x591b28,0x56de58,0x581970,0x58ddf8,0x57e670,0x57b090,
         0x57b070,0x578898,0x576c40,0x580cd8,0x58cdd8,0x56d928,0x575a38,0x574da0,0x578ae0,
         0x568d70,0x56b040,0x569028]
live_callbacks=[0x29aed0,0x2c0ff8,0x1eab58,0x34d310,0x2d30d0,0x11fab8,0x3143f0,
                0x2c0fa8,0x1583f0,0x181a58,0x36d538,0x29ce28,0x1b2288,0x2c96c8,
                0x14ed88,0x14e7f8,0x2dc548,0x36d548,0x35ceb8,0x2af980,0x166880,0x121038]
known={int(r['start'],0) for r in csv.DictReader((GAME/'config/kernel-functions.csv').open())}
source=(GAME/'runtime/output/register_functions.cpp').read_text()
known.update(int(x,16) for x in re.findall(r'// (0x[0-9a-f]+)',source))
# Original zero-return predicate prevents the following optional null call.
assert [word(0x36d548+i) for i in (0,4,8)] == [0x0000102d,0x03e00008,0]
assert word(0x31af94)==0x1040000b  # BEQ v0,zero -> 0x31afc4
assert 0x31af98 + 11*4 == 0x31afc4
assert word(0x574698+0x3c)==0x36d548 and word(0x574698+0xdc)==0
null_guard=dict(predicate='0x36d548', return_value=0, branch='0x31af94',
                safe_target='0x31afc4', optional_call='0x31afac',
                stale_nonzero_value='0x36d548', optional_table_target=0)
tables=[]
for address in anchors:
    assert [word(address+i) for i in (0,4,8)]==[0,0,0],hex(address)
    methods=[]
    for offset in range(12,512,8):
        value=word(address+offset)
        if value==0:break
        assert 0x100000<=value<0x4b2000 and value%4==0,hex(address+offset)
        assert word(address+offset-4)==0,hex(address+offset-4)
        methods.append(value)
    else:raise AssertionError('Unterminated table '+hex(address))
    assert methods
    tables.append(dict(address=hex(address),methods=list(map(hex,methods)),
                       missing=[hex(v) for v in methods if v not in known]))
missing=sorted({int(v,16) for t in tables for v in t['missing']} | {v for v in live_callbacks if v not in known})
report=dict(elf_sha256=digest,scope=f'{len(tables)} live-anchored tables plus observed dynamic callbacks; Ghidra boundary review required',
            live_callbacks=list(map(hex,live_callbacks)),null_call_guard=null_guard,
            tables=tables,missing=list(map(hex,missing)))
(GAME/'logs/level-object-vtables.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'{len(tables)} live tables; {len(missing)} missing entries')
print(' '.join(report['missing']))
