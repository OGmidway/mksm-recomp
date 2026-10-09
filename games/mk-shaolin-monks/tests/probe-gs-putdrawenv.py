"""Execute retail sceGsPutDrawEnv's DMA submission path with a bounded MIPS probe."""
import importlib.util, json, pathlib, struct
GAME=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('symbols', GAME/'tests/transfer-prototype-symbols.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
MASK=(1<<64)-1
def signed(v,bits):
 v&=(1<<bits)-1
 return v-(1<<bits) if v>>(bits-1) else v

def execute(read,address,tag,busy_reads=0):
 r=[0]*32;r[4]=address;r[29]=0x1e00000;r[31]=0x80000
 pc=0x383668;pending=None;memory={address:tag};writes=[]
 for cycle in range(256):
  if pc==0x80000:return writes,r[2]
  w=struct.unpack('<I',read(pc,4))[0];op=w>>26;rs=w>>21&31;rt=w>>16&31;rd=w>>11&31;sa=w>>6&31;fn=w&63;imm=w&65535;si=signed(imm,16)
  old=pending;pending=None
  if w==0:pass
  elif op==15:r[rt]=signed(imm<<16,32)
  elif op==13:r[rt]=r[rs]|imm
  elif op==12:r[rt]=r[rs]&imm
  elif op==9:r[rt]=signed(r[rs]+si,32)
  elif op in (35,55):
   a=(r[rs]+si)&0xffffffff
   if a==0x1000a000:r[rt]=0x100 if busy_reads else 0;busy_reads=max(0,busy_reads-1)
   else:r[rt]=memory[a]
  elif op in (43,63):
   a=(r[rs]+si)&0xffffffff;v=r[rt]&((1<<(32 if op==43 else 64))-1);memory[a]=v
   if 0x1000a000<=a<=0x1000a020:writes.append((a,v))
  elif op in (4,5):
   if (r[rs]==r[rt])==(op==4):pending=pc+4+si*4
  elif op==0 and fn==8:pending=r[rs]&0xffffffff
  elif op==0 and fn==45:r[rd]=r[rs]+r[rt]
  elif op==0 and fn==36:r[rd]=r[rs]&r[rt]
  elif op==0 and fn==37:r[rd]=r[rs]|r[rt]
  elif op==0 and fn==43:r[rd]=int(r[rs]<r[rt])
  elif op==0 and fn==60:r[rd]=r[rt]<<(sa+32)
  elif op==0 and fn==63:r[rd]=signed(r[rt],64)>>(sa+32)
  else:raise ValueError(f'unsupported {w:08x} at {pc:x}')
  r=[v&MASK for v in r];r[0]=0;pc=old if old is not None else pc+4
 raise ValueError('instruction limit')

def main():
 read=s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',s.HASHES['retail']);cases=[]
 for address in (0x90000,0x20090000,0x80090000,0x70000100):
  for loops in (0,8,14,22,32767):
   for busy in (0,3):
    writes,result=execute(read,address,0x1000000000008000|loops,busy)
    madr=(address&0xfffffff)|(0x80000000 if address&0x70000000==0x70000000 else 0)
    assert result==0 and writes==[(0x1000a020,loops+1),(0x1000a010,madr),(0x1000a000,0x101)]
    cases.append({'address':address,'loops':loops,'busy_reads':busy,'writes':writes})
 (GAME/'logs/gs-putdrawenv-probe.json').write_text(json.dumps({'retail_sha256':s.HASHES['retail'],'passed':True,'cases':cases},indent=2))
 print(f'PASS: {len(cases)} retail DMA submissions include GIF tag and complete NLOOP payload')
if __name__=='__main__':main()
