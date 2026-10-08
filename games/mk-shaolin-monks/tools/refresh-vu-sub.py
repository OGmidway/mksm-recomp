"""Refresh only stale VSUB statements in existing generated retail output."""
from pathlib import Path
import re,struct,hashlib,json
GAME=Path(__file__).resolve().parents[1];ROOT=GAME.parents[1]
blob=(ROOT/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
assert hashlib.sha256(blob).hexdigest()=='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
ph=struct.unpack_from('<I',blob,28)[0];stride,count=struct.unpack_from('<HH',blob,42)
segments=[struct.unpack_from('<8I',blob,ph+i*stride) for i in range(count)]
def word(a):
 for kind,offset,base,_,size,*_ in segments:
  if kind==1 and base<=a and a+4<=base+size:return struct.unpack_from('<I',blob,offset+a-base)[0]
 raise ValueError(hex(a))
changes=[]
for folder in ('output','kernel-output'):
 for p in (GAME/'runtime'/folder).glob('*.cpp'):
  original=p.read_text();lines=original.splitlines(keepends=True);instruction=None;sites=[]
  for i,line in enumerate(lines):
   m=re.search(r'// (0x[0-9a-f]+): (0x[0-9a-f]+)\s+(vsub(?:\.[a-z]+)?|vsub[xyzwqi](?:\.[a-z]+)?)\s',line)
   if '// 0x' in line:instruction=(int(m[1],16),int(m[2],16)) if m else None
   if 'PS2_VSUB(' in line and 'ctx->vu0_vf[' in line and '= ' in line and instruction:
    address,raw=instruction;assert word(address)==raw
    assert (raw&63) in (4,5,6,7,0x24,0x26,0x2c) and 'ctx->vu0_acc' not in line
    assert line.strip().startswith('{ __m128 res = PS2_VSUB(') and line.strip().endswith('}')
    lines[i]=line[:len(line)-len(line.lstrip())]+f'ps2VuSub(ctx, 0x{raw:08x}u);\n';sites.append(hex(address))
  if sites:
   updated=''.join(lines);include='#include "runtime/ps2_vu_sub.h"\n'
   if include not in updated:updated=include+updated
   p.write_text(updated);changes.append(dict(file=str(p.relative_to(GAME)),sites=sites))
report=dict(files=len(changes),statements=sum(len(c['sites']) for c in changes),changes=changes)
(GAME/'logs/vu-sub-refresh.json').write_text(json.dumps(report,indent=2)+'\n')
print(f"Updated {report['statements']} verified retail VSUB statements in {report['files']} generated files")
