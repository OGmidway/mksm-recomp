"""Scalar CLIP plane references and original retail visibility-mask reproduction."""
from pathlib import Path
import json,struct
G=Path(__file__).resolve().parents[1]
def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def clip(v,w,history=0):
    limit=(w&0x7fffffff) if w&0x7f800000 else 0x7fffff
    flags=0
    for i,x in enumerate(v[:3]):
        if x&0x7fffffff>limit:flags|=1<<(2*i+(x>>31))
    return ((history<<6)|flags)&0xffffff
vectors=[]
for axis in range(3):
 for sign in (-1,1):
  v=[0.,0.,0.,777.];v[axis]=2*sign
  for w in (1.,-1.):vectors.append(([bits(x) for x in v],bits(w),0))
for v,w in (([0,0,0,0],0),([1,0x80000001,0x7fffff,0],0),([0x800000,0x80800000,0,0],1),([0x7f800000,0xff800000,0x7fc00000,0],0x3f800000),([0x3f800000,0xbf800000,0,0],0x3f800000)):
 vectors.append((v,w,0xabcdef))
rows=[(v,w,h,clip(v,w,h)) for v,w,h in vectors]
fmt=lambda x:f'0x{x:08x}u'
(G/'tests/vu-clip-cases.inc').write_text('\n'.join('{ {'+','.join(map(fmt,v))+'},'+','.join(map(fmt,(w,h,e)))+'},' for v,w,h,e in rows)+'\n')
def classify(flags):return 0 if flags&0xaaa else 1 if flags&0x555==0x555 else 2
correct=clip([bits(2.)]*3,bits(1.));correct=clip([bits(2.)]*3,bits(1.),correct)
legacy=((correct&0x555)<<1)|((correct&0xaaa)>>1)
assert classify(correct)==1 and classify(legacy)==0
report=dict(cases=len(rows),positive_planes_inside=dict(correct_flags=hex(correct),legacy_flags=hex(legacy),correct_result=classify(correct),legacy_result=classify(legacy)),reference='PCSX2 VUops.cpp _vuCLIP; runtime VU1 CLIP agrees. Retail 0x22c838 uses 0xaaa/0x555 masks.')
(G/'logs/vu-clip-probe.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
