"""Execute retail IOP SJU instructions and generate portable descriptor-queue fixtures."""
import hashlib,json,pathlib,struct
GAME=pathlib.Path(__file__).resolve().parents[1]
IRX=GAME.parents[1]/'MortalKombatShaolinMonks/IOP/cri_adxi.irx'
SHA='2816b100b6bfa398b317f783aa8f314d28b54cd6ad1240988bc9d0c62f53c7b5'
OBJ,WORK,OUT,STACK,RETURN=0x20000,0x21000,0x22000,0x3f000,0x3fffc

def signed(v,b=32):
 v&=(1<<b)-1
 return v-(1<<b) if v>>(b-1) else v

class Iop:
 def __init__(self,mode,capacity):
  data=IRX.read_bytes();assert hashlib.sha256(data).hexdigest()==SHA
  self.ram=bytearray(0x40000)
  h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
  for i in range(h[12]):
   x=struct.unpack_from('<10I',data,h[6]+i*h[11])
   if x[1]==1 and x[2]&2:self.ram[x[3]:x[3]+x[5]]=data[x[4]:x[4]+x[5]]
  self.put(OBJ+4,1|(mode<<8));self.put(OBJ+12,WORK);self.put(OBJ+16,capacity)
  self.capacity=capacity;self.call(0x7d80,[OBJ])
 def get(self,a,n=4):
  assert 0<=a<=len(self.ram)-n
  return int.from_bytes(self.ram[a:a+n],'little')
 def put(self,a,v,n=4):
  assert 0<=a<=len(self.ram)-n
  self.ram[a:a+n]=(v&((1<<(n*8))-1)).to_bytes(n,'little')
 def call(self,pc,args):
  r=[0]*32;r[4:4+len(args)]=args;r[29]=STACK;r[31]=RETURN
  pending=None;pending_load=None;hi=lo=0
  for i,v in enumerate(args[4:]):self.put(STACK+16+i*4,v)
  for _ in range(getattr(self,"instruction_limit",5000)):
   if pc==RETURN:return r[2]
   hook=getattr(self,'hooks',{}).get(pc)
   if hook:
    if pending_load:r[pending_load[0]]=pending_load[1];pending_load=None
    assert pending is None
    r[2]=hook(r)&0xffffffff;pc=r[31];continue
   w=self.get(pc);op=w>>26;rs=w>>21&31;rt=w>>16&31;rd=w>>11&31;sa=w>>6&31;fn=w&63
   imm=w&65535;si=signed(imm,16);old=pending;pending=None;nxt=pc+4;load=None;dest=None
   def reg(i,v):
    nonlocal dest
    r[i]=v&0xffffffff;dest=i
   if w==0:pass
   elif op==15:reg(rt,imm<<16)
   elif op==9:reg(rt,r[rs]+si)
   elif op==10:reg(rt,int(signed(r[rs])<si))
   elif op==11:reg(rt,int(r[rs]<(si&0xffffffff)))
   elif op==12:reg(rt,r[rs]&imm)
   elif op==13:reg(rt,r[rs]|imm)
   elif op==14:reg(rt,r[rs]^imm)
   elif op in (32,33,35,36,37):
    n={32:1,33:2,35:4,36:1,37:2}[op];v=self.get((r[rs]+si)&0xffffffff,n)
    load=(rt,signed(v,n*8)&0xffffffff if op in (32,33) else v)
   elif op in (40,41,43):self.put((r[rs]+si)&0xffffffff,r[rt],{40:1,41:2,43:4}[op])
   elif op in (2,3):
    if op==3:reg(31,pc+8)
    pending=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
   elif op in (1,4,5,6,7):
    if op==1:
     assert rt in (0,1)
     taken=(signed(r[rs])<0)==(rt==0)
    elif op in (4,5):taken=(r[rs]==r[rt])==(op==4)
    else:taken=signed(r[rs])<=0 if op==6 else signed(r[rs])>0
    if taken:pending=pc+4+si*4
   elif op==0:
    if fn==0:reg(rd,r[rt]<<sa)
    elif fn==2:reg(rd,r[rt]>>sa)
    elif fn==3:reg(rd,signed(r[rt])>>sa)
    elif fn in (4,6,7):
     shift=r[rs]&31
     reg(rd,r[rt]<<shift if fn==4 else (r[rt]>>shift if fn==6 else signed(r[rt])>>shift))
    elif fn==16:reg(rd,hi)
    elif fn==18:reg(rd,lo)
    elif fn in (24,25):
     value=(signed(r[rs])*signed(r[rt]) if fn==24 else r[rs]*r[rt])&0xffffffffffffffff
     hi=value>>32;lo=value&0xffffffff
    elif fn in (26,27):
     a,b=(signed(r[rs]),signed(r[rt])) if fn==26 else (r[rs],r[rt])
     if not b:raise ValueError('division by zero not modeled')
     q=abs(a)//abs(b)
     if (a<0)!=(b<0):q=-q
     lo=q&0xffffffff;hi=(a-q*b)&0xffffffff
    elif fn in (8,9):
     pending=r[rs]
     if fn==9:reg(rd,pc+8)
    elif fn in (33,35):reg(rd,r[rs]+(r[rt] if fn==33 else -r[rt]))
    elif fn==36:reg(rd,r[rs]&r[rt])
    elif fn==37:reg(rd,r[rs]|r[rt])
    elif fn==38:reg(rd,r[rs]^r[rt])
    elif fn==39:reg(rd,~(r[rs]|r[rt]))
    elif fn==42:reg(rd,int(signed(r[rs])<signed(r[rt])))
    elif fn==43:reg(rd,int(r[rs]<r[rt]))
    else:raise ValueError(f'unsupported SPECIAL {w:08x} at {pc:x}')
   else:raise ValueError(f'unsupported {w:08x} at {pc:x}')
   if pending_load and pending_load[0]!=dest:r[pending_load[0]]=pending_load[1]
   pending_load=load;r[0]=0;pc=old if old is not None else nxt
  raise ValueError('instruction limit')
 def lines(self):
  result=[];seen=set()
  for line in range(4):
   p=self.get(OBJ+24+line*4);q=[]
   while p:
    assert WORK<=p<WORK+self.capacity*16 and p not in seen
    seen.add(p);q.append([self.get(p+8),self.get(p+12)]);p=self.get(p)
   result.append(q)
  p=self.get(OBJ+20)
  while p:
   assert WORK<=p<WORK+self.capacity*16 and p not in seen
   seen.add(p);p=self.get(p)
  assert len(seen)==self.capacity
  return result

def main():
 cases=[]
 for mode in (0,1):
  cpu=Iop(mode,3)
  # Exercises all four queues, non-contiguous chunks, exhaustion, coalescing,
  # splitting, prepend/unget, descriptor reuse and empty get.
  ops=[(0,1,0x30000,64),(0,1,0x30040,32),(0,1,0x31000,16),
       (0,0,0x32000,8),(2,1,48,0),(1,1,0x30000,48),
       (3,1,32,0),(2,1,1000,0),(0,2,0x33000,4),(0,3,0x34000,4),
       (2,1,1000,0),(2,0,1000,0),(2,2,1000,0),(2,3,1000,0),
       (2,1,1000,0),(0,1,0,1),(0,1,0x30000,0)]
  for op,line,a,b in ops:
   before=cpu.lines();cpu.put(OUT,0);cpu.put(OUT+4,0)
   if op in (0,1):
    cpu.put(OUT,a);cpu.put(OUT+4,b)
    cpu.call(0x8164 if op==0 else 0x8310,[OBJ,line,OUT]);out=[0,0]
   elif op==2:
    cpu.call(0x7fbc,[OBJ,line,a,OUT]);out=[cpu.get(OUT),cpu.get(OUT+4)]
   else:out=[cpu.call(0x84bc,[OBJ,line,a,OUT]),cpu.get(OUT)]
   lines=cpu.lines()
   counts=[cpu.call(0x7ea0,[OBJ,i]) for i in range(4)]
   assert counts==[sum(n for _,n in q) for q in lines]
   cases.append({'mode':mode,'op':op,'line':line,'a':a,'b':b,'out':out,'lines':lines,'counts':counts,'unchanged':lines==before})
 out={'irx_sha256':SHA,'scope':'Original IOP integer instructions including load and branch delay slots','cases':cases,'passed':True}
 (GAME/'logs/cri-queue-probe.json').write_text(json.dumps(out,indent=2)+'\n')
 rows=['// Original retail IOP results; generated by probe-cri-queue.py.']
 for c in cases:
  flat=[(x[0],x[1]) for q in c['lines'] for x in q];flat+=[(0,0)]*(3-len(flat))
  rows.append('{'+','.join(map(str,[c['mode'],c['op'],c['line'],c['a'],c['b']]))+', {'+','.join(map(str,c['out']))+'}, {'+','.join(map(str,c['counts']))+'}, {'+','.join(str(len(q)) for q in c['lines'])+'}, {'+','.join('{'+f'{p},{n}'+'}' for p,n in flat)+'}},')
 (GAME/'tests/cri_queue_cases.inc').write_text('\n'.join(rows)+'\n')
 print(f'PASS: {len(cases)} original IOP queue steps, four lines and descriptor conservation')
if __name__=='__main__':main()
