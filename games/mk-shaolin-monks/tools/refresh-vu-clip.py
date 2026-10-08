"""Refresh only verified retail macro VCLIP statements; generator supplies future output."""
from pathlib import Path
import re,struct,hashlib,json
G=Path(__file__).resolve().parents[1];R=G.parents[1]
b=(R/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
assert hashlib.sha256(b).hexdigest()=='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
ph=struct.unpack_from('<I',b,28)[0];stride,count=struct.unpack_from('<HH',b,42)
segments=[struct.unpack_from('<8I',b,ph+i*stride) for i in range(count)]
def word(a):
 for kind,off,base,_,size,*_ in segments:
  if kind==1 and base<=a and a+4<=base+size:return struct.unpack_from('<I',b,off+a-base)[0]
 raise ValueError(hex(a))
changes=[]
for folder in ('output','kernel-output'):
 for p in (G/'runtime'/folder).glob('*.cpp'):
  lines=p.read_text().splitlines(keepends=True);instruction=None;sites=[]
  for i,line in enumerate(lines):
   if '// 0x' in line:
    m=re.search(r'// (0x[0-9a-f]+): (0x[0-9a-f]+)',line)
    instruction=(int(m[1],16),int(m[2],16)) if m and 'vclipw.xyz' in line else None
   if 'uint32_t flags = ((lt_mask' in line and instruction:
    address,raw=instruction;assert word(address)==raw
    assert (raw&63)==63 and ((raw>>6)&31)==7
    assert line.strip().startswith('{ __m128 fs =') and line.strip().endswith('}')
    lines[i]='    ps2VuClip(ctx, 0x%08xu);\n'%raw;sites.append(hex(address))
  if sites:
   s=''.join(lines);include='#include "runtime/ps2_vu_clip.h"\n'
   if include not in s:s=include+s
   p.write_text(s);changes.append(dict(file=str(p.relative_to(G)),sites=sites))
report=dict(files=len(changes),statements=sum(len(x['sites']) for x in changes),changes=changes)
if changes:(G/'logs/vu-clip-refresh.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='changes'}))
