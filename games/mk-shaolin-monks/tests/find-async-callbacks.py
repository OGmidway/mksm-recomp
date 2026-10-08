"""Find constant callback arguments at retail Async registration callsites."""
import pathlib,struct,importlib.util,json
GAME=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('s',GAME/'tests/transfer-prototype-symbols.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
read=s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',s.HASHES['retail']);base=0x100000;code=read(base,0x39b000);words=struct.unpack('<'+'I'*(len(code)//4),code);rows=[]
for i,w in enumerate(words):
 if w>>26!=3 or (w&0x3ffffff)*4!=0x379078:continue
 regs=[None]*32;regs[0]=0
 # Only propagate through the final straight-line block; calls/branches invalidate it.
 for pos in range(max(0,i-40),i+2):
  x=words[pos];op=x>>26;rs=x>>21&31;rt=x>>16&31;rd=x>>11&31;imm=x&65535;si=imm-65536 if imm&32768 else imm
  if pos==i:continue # The callback is consumed after this call's delay slot.
  if x==0:continue
  if op==15:regs[rt]=imm<<16
  elif op in (9,25,13):regs[rt]=None if regs[rs] is None else ((regs[rs]|imm) if op==13 else (regs[rs]+si)&0xffffffff)
  elif op==0 and x&63 in (33,45,37):regs[rd]=None if regs[rs] is None or regs[rt] is None else ((regs[rs]|regs[rt]) if x&63==37 else (regs[rs]+regs[rt])&0xffffffff)
  elif op in (1,2,3,4,5,6,7,20,21,22,23) or (op==0 and x&63 in (8,9)):regs=[None]*32
  elif op in (40,41,42,43,44,45,46,47,57,58,61,62,63):pass
  elif op in (56,60):regs[rt]=None # SC/SCD write their success result to rt.
  elif op==0:regs[rd]=None
  elif op in (16,17,18):regs=[None]*32 # conservative around coprocessor code
  else:regs[rt]=None
  regs[0]=0
 a=regs[8]
 if a is not None and 0x100000<=a<0x49b000 and a%4==0:rows.append({'callsite':hex(base+i*4),'callback':hex(a)})
report={'retail_sha256':s.HASHES['retail'],'registration':'0x379078','argument_register':8,'evidence':'Constant propagation through final straight-line block including delay slot; candidates require disassembled function boundaries','callsites':rows,'callbacks':sorted({x['callback'] for x in rows},key=lambda x:int(x,16))}
(GAME/'logs/async-callback-recovery.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
