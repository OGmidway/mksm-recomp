"""Execute original retail leaf debug routines in a small, fail-closed MIPS probe."""
import importlib.util,pathlib,struct,json
spec=importlib.util.spec_from_file_location('symbols',pathlib.Path(__file__).with_name('transfer-prototype-symbols.py'));s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
def execute(read,start,memory,argument=0):
 regs=[0]*32;regs[4]=argument;regs[31]=0x80000;pc=start;pending=None;writes=[]
 def signed(v):return v-0x100000000 if v&0x80000000 else v
 for count in range(1000):
  if pc==0x80000:return writes
  w=struct.unpack('<I',read(pc,4))[0];op=w>>26;rs=w>>21&31;rt=w>>16&31;rd=w>>11&31;imm=w&65535;si=imm-65536 if imm&32768 else imm
  branch=None;old=pending;pending=None
  if w==0:pass
  elif op==15:regs[rt]=imm<<16
  elif op==9:regs[rt]=(regs[rs]+si)&0xffffffff
  elif op==11:regs[rt]=int(regs[rs]<(si&0xffffffff))
  elif op==35:regs[rt]=memory.get((regs[rs]+si)&0xffffffff,0)
  elif op==43:
   a=(regs[rs]+si)&0xffffffff;memory[a]=regs[rt];writes.append((a,regs[rt]))
  elif op in (4,5):
   if (regs[rs]==regs[rt])==(op==4):branch=pc+4+si*4
  elif op==0 and w&63 in (33,45):regs[rd]=(regs[rs]+regs[rt])&0xffffffff
  elif op==0 and w&63==24:regs[rd]=(signed(regs[rs])*signed(regs[rt]))&0xffffffff
  elif op==0 and w&63==8:branch=regs[rs]
  else:raise ValueError(f'Unsupported instruction {w:08x} at {pc:x}')
  regs[0]=0;pending=branch;pc=old if old is not None else pc+4
 raise ValueError('Probe step limit')
def main():
 read=s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',s.HASHES['retail']);cases=[]
 for object_index in (0,1,3):
  for freeze in (False,True):
   trigger=0x71e4e0+2*0x600-0x570
   memory={0x50c134:object_index,0x5e3780+object_index*0x33c0+0x68:2,trigger:0xabcdef}
   writes=execute(read,0x1aa5b8 if freeze else 0x1aa558,memory)
   assert memory[0x511378]==(4 if freeze else 1) and memory[0x5d1e4c]==1
   assert memory[trigger]==(1 if object_index else 0xabcdef)
   if freeze:assert memory[0x519ab4]==1 and memory[0x6bee10]==1
   allowed={0x511378,0x5d1e4c}|({trigger} if object_index else set())|({0x519ab4,0x6bee10} if freeze else set())
   assert {a for a,v in writes}==allowed
   cases.append({'routine':'SelectFreezeCam' if freeze else 'SelectFreeCam','object_index':object_index,'writes':writes})
 for god in (0,1,2):
  memory={0x4c7778:god,0x90000:0x666,0x90008:0x667,0x90010:99,0x90018:0xffffffff}
  writes=execute(read,0x1aae10,memory,0x90000)
  assert memory[0x90000]==memory[0x90008]==(0x667 if god==1 else 0x666)
  assert memory[0x90010]==99 and memory[0x90018]==0xffffffff
  cases.append({'routine':'SetGodModeText','god_mode':god,'writes':writes})
 out=s.GAME/'logs/debug-feature-probe.json';out.write_text(json.dumps({'retail_sha256':s.HASHES['retail'],'cases':cases,'passed':True},indent=2)+'\n')
 print(f'PASS: {len(cases)} original-retail instruction probes; camera stride, null object, flags and localized god-mode IDs')
if __name__=='__main__':main()
