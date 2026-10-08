"""Retail-opcode-checked scalar references for SDK normalization and projection flags."""
from pathlib import Path
import hashlib,json,math,struct
G=Path(__file__).resolve().parents[1];R=G.parents[1]
b=(R/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
assert hashlib.sha256(b).hexdigest()=='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
ph=struct.unpack_from('<I',b,28)[0];stride,n=struct.unpack_from('<HH',b,42)
def words(a,count):
 for i in range(n):
  kind,off,base,_,size,*_=struct.unpack_from('<8I',b,ph+i*stride)
  if kind==1 and base<=a and a+count*4<=base+size:return struct.unpack_from('<'+str(count)+'I',b,off+a-base)
 raise ValueError(hex(a))
assert words(0x3853b0,15)==(0xd8a40000,0x4bc4216a,0x4b052941,0x4b052942,0x4a0503bd,0x4a0003bf,0x4b000160,0x4a0002ff,0x4a0002ff,0x4a6503bc,0x4be001ac,0x4a0003bf,0x4bc0219c,0x03e00008,0xf8860000)
assert words(0x3858c0,17)==(0xd8a40000,0xd8a50010,0xd8a60020,0xd8a70030,0xd8c80000,0x4be821bc,0x4be828bd,0x4be830be,0x4be83a4b,0x4be903bc,0x4a0003bf,0x4bc04a5c,0x10e00002,0x4bea497d,0x4a6a497c,0x03e00008,0xf88a0000)
assert words(0x213e08,9)==(0xd8840000,0xd8a50000,0x4bc5216a,0x4b052941,0x4b052942,0x48222800,0x44820000,0x03e00008,0)
f=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
def norm(v):
 # VMUL.xyz; VADDy.x; VADDz.x; VSQRT; VDIV 1/len; zero vf6; VMULQ.xyz.
 length=f(math.sqrt(f(f(f(v[0]*v[0])+f(v[1]*v[1]))+f(v[2]*v[2]))))
 q=f(1/length) if length else f(3.4028234663852886e38)
 return [f(x*q) for x in v[:3]]+[0.0]
def project(m,v,mode):
 # Accumulator column products, Q=1/w, XYZ divide; branch preserves all FTOI4 for mode 0.
 t=[]
 for c in range(4):
  x=f(m[c]*v[0])
  for k in range(1,4):x=f(x+f(m[k*4+c]*v[k]))
  t.append(x)
 assert t[3]!=0,'Zero/overflow PS2 arithmetic is outside these finite reference cases'
 q=f(1/t[3]);t[:3]=[f(x*q) for x in t[:3]]
 return [math.trunc(f(x*(16 if c<2 or mode==0 else 1))) for c,x in enumerate(t)]
vecs=[[3,4,0,12],[0,0,2,1000],[1,2,2,-7],[f(1e-7),0,0,1],[0,0,0,4]]
matrices=[[1.,0,0,0,0,1.,0,0,0,0,1.,0,0,0,0,1.],[2.,3.,0,0,0,1.,4.,0,5.,0,1.,0,10.,20.,30.,1.]]
projections=[(m,v,mode,project(m,v,mode)) for m in matrices for v in ([1.,2.,3.,4.],[-3.,5.,7.,-2.]) for mode in (0,1,7)]
fmt=lambda xs:'{'+','.join(f'0x{struct.unpack("<I",struct.pack("<f",x))[0]:08x}u' for x in xs)+'}'
lines=['// Generated from verified retail SDK instructions by probe-vu-sdk.py.', 'struct NormCase { uint32_t source[4],expected[4]; };','static const NormCase normCases[] = {']
lines += ['{'+fmt(v)+','+fmt(norm(v))+'},' for v in vecs];lines+=['};','struct PersCase { uint32_t matrix[16],source[4],mode; int32_t expected[4]; };','static const PersCase persCases[] = {']
lines+=['{'+fmt(m)+','+fmt(v)+','+str(mode)+',{' + ','.join(map(str,out))+'}},' for m,v,mode,out in projections];lines+=['};']
dots=[([1.,2.,3.,4.],[5.,6.,7.,8.]),([0.,0,0,1.],[0.,0,0,1.]),([-3.,4.,2.,-999.],[7.,-2.,5.,777.])]
lines+=['struct DotCase { uint32_t left[4],right[4],expected; };','static const DotCase dotCases[] = {']
for left,right in dots:
 expected=f(f(f(left[0]*right[0])+f(left[1]*right[1]))+f(left[2]*right[2]))
 lines+=['{'+fmt(left)+','+fmt(right)+','+fmt([expected])[1:-1]+'},']
lines+=['};']
(G/'tests/vu-sdk-cases.inc').write_text('\n'.join(lines)+'\n')
report=dict(dot_cases=len(dots),normalize_cases=len(vecs),projection_cases=len(projections),scope='Finite scalar references and zero-vector normalization; not cycle-exact or exhaustive PS2 floating-point emulation.',normalize_uses_xyz=True,normalize_output_w=0,projection_mode_zero_all_ftoi4=True)
(G/'logs/vu-sdk-probe.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
