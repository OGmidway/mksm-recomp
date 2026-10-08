"""Run retail SNDF initialization/partition instructions; no host audio emulation."""
import hashlib, importlib.util, json, struct, zlib
from pathlib import Path
GAME=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cri_probe', Path(__file__).with_name('probe-cri-queue.py'))
cri=importlib.util.module_from_spec(spec);spec.loader.exec_module(cri)
SHA='3bff7898bc43c5592a6cfeccbae77714d935de706a5b138b98cd5255aca1f968'
DRIVER,STATUS,BANK,ARGS=0x20000,0x30000,0x40000,0x38000
class Sndf(cri.Iop):
 def __init__(self):
  data=(GAME.parents[1]/'MortalKombatShaolinMonks/IOP/sndfi.irx').read_bytes()
  assert hashlib.sha256(data).hexdigest()==SHA
  self.ram=bytearray(0x80000);self.instruction_limit=100000
  h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
  for i in range(h[12]):
   x=struct.unpack_from('<10I',data,h[6]+i*h[11])
   if x[1]==1 and x[2]&2:self.ram[x[3]:x[3]+x[5]]=data[x[4]:x[4]+x[5]]
  self.put(0x12910,DRIVER);self.put(0x12918,STATUS)
  # Hardware register writes/transfer setup have no CPU-visible result here.
  self.hardware=[]
  def hardware(pc):
   def call(r):self.hardware.append([pc,*r[4:8]]);return 0
   return call
  self.hooks={p:hardware(p) for p in (0x210,0x218,0x238,0x240,0x250,0x258)}
  def memset(r):self.ram[r[4]:r[4]+r[6]]=bytes([r[5]&255])*r[6];return r[4]
  self.hooks[0x13c]=memset
  self.switches={}
  def set_switch(r):self.hardware.append([0x228,*r[4:8]]);self.switches[r[4]&0xffff]=r[5]&0xffffff;return 0
  def get_switch(r):
   key=r[4]&0xffff
   if key not in self.switches:raise ValueError(f'unseeded SPU switch read {key:x}')
   return self.switches[key]
  self.hooks[0x228]=set_switch;self.hooks[0x230]=get_switch
  self.interrupts=1
  def suspend(r):self.put(r[4],self.interrupts);self.interrupts=0;return 0
  def resume(r):self.interrupts=r[4];return 0
  self.hooks[0x40]=suspend;self.hooks[0x48]=resume
  for pc in range(0,0x268,4):
   if self.get(pc)==0x03e00008 and pc not in self.hooks:
    def trap(r,pc=pc):raise ValueError(f'unmodeled import {pc:x} args={r[4:8]}')
    self.hooks[pc]=trap
 def initialize(self):
  assert self.call(0xb678,[0x30000,0,0,1,0,0])==0
  self.call(0xc3d0,[])
  # Retail 462918 computes log(48000/48000)/log(2)*1200 = 0.
  # b7b8 stores that signed pitch base at +24, not a flags word.
  self.put(DRIVER+0x24,0)
  # AllocSysMemory and b7b8 install these addresses after c3d0.
  self.put(DRIVER+0x728,BANK);self.put(DRIVER+0x3e8,BANK);self.put(DRIVER+0x568,0x20000)
 def layout(self,headers,waves):
  for i,v in enumerate(headers+waves):self.put(ARGS+i*4,v)
  return cri.signed(self.call(0xe400,[ARGS,ARGS+192]))
 def bank_header(self,slot,version=0x4702,count=3,magic=True):
  p=self.get(DRIVER+0x3e8+slot*4)
  self.ram[p:p+0x100]=bytes(0x100)
  self.ram[p:p+8]=b'ps2_DTPK' if magic else b'invalid!'
  self.put(p+8,version,2);self.put(p+0x10,0x100);self.put(p+0x18,0x200)
  self.put(p+0x84,123+slot);self.put(p+0x89,count,1)
  return p

def retail_bank(file_id):
 with (GAME.parents[1]/'MortalKombatShaolinMonks/GAMEDATA.WAD').open('rb') as f:
  f.seek(0x20+(file_id-30)*8);value=struct.unpack('<Q',f.read(8))[0]
  packed=bool(value>>63)
  offset=(value<<11)&0xffffffff if packed else value&0xffffffff
  size=(value>>22)&0x3fffff if packed else value>>32
  f.seek(offset);data=f.read(size)
 if packed:
  assert data[:4]==b'EWDF'
  expected,start=struct.unpack_from('<II',data,4)
  data=zlib.decompress(data[start:],-15);assert len(data)==expected
 assert data[:8]==b'ps2_DTPK'
 return data


def retail_registration():
 cpu=Sndf();cpu.initialize();banks=[retail_bank(i) for i in (36,38,57)]
 headers=[struct.unpack_from('<I',b,0x10)[0] for b in banks]
 waves=[struct.unpack_from('<I',b,0x18)[0] for b in banks]
 assert cpu.layout(headers+[0]*45,waves+[0]*45)==0
 fixture=bytearray(b'SBNK'+struct.pack('<I',3));rows=[]
 for slot,bank in enumerate(banks):
  base=cpu.get(DRIVER+0x3e8+slot*4);cpu.ram[base:base+headers[slot]]=bank[:headers[slot]]
  result=cri.signed(cpu.call(0xe1d0,[slot]));assert result==0
  tone_bytes=cpu.ram[DRIVER+0xf7c:DRIVER+0xf7c+512*28]
  fingerprint=14695981039346656037
  for b in tone_bytes:fingerprint=((fingerprint^b)*1099511628211)&0xffffffffffffffff
  fixture+=struct.pack('<IIIQ',base,headers[slot],waves[slot],fingerprint)
  fixture+=bank[:headers[slot]]+cpu.ram[STATUS:STATUS+0x2b00]
  wave_start=struct.unpack_from('<I',bank,0x1c)[0]
  assert wave_start+waves[slot]<=len(bank)
  wave_hash=14695981039346656037
  for b in bank[wave_start:wave_start+waves[slot]]:wave_hash=((wave_hash^b)*1099511628211)&0xffffffffffffffff
  rows.append(dict(wave_bytes=waves[slot],wave_fingerprint=hex(wave_hash),slot=slot,file_id=(36,38,57)[slot],bank_id=struct.unpack_from('<I',bank,0x84)[0],sha256=hashlib.sha256(bank).hexdigest(),result=result,tones=sum(tone_bytes[i*28]!=0 for i in range(512)),tone_fingerprint=fingerprint))
 assert len(fixture)<100*1024
 registered=bytes(cpu.ram)
 unloads=[]
 # Execute actual consumer and bank teardown, preserving the later voice probe.
 cpu.call(0x6670,[0xa0000100,0x20,255])
 fixture+=struct.pack('<I',4)
 for slot in (2,1,0,2):
  result=cri.signed(cpu.call(0xde2c,[slot]))
  fingerprint=14695981039346656037
  for b in cpu.ram[DRIVER+0xf7c:DRIVER+0xf7c+512*28]:fingerprint=((fingerprint^b)*1099511628211)&0xffffffffffffffff
  fixture+=struct.pack('<IiIQ',slot,result,cpu.get(DRIVER+0x16,2),fingerprint)+cpu.ram[STATUS:STATUS+0x2b00]
  unloads.append(dict(slot=slot,result=result,waves=cpu.get(DRIVER+0x16,2),tone_fingerprint=fingerprint))
 assert len(fixture)<100*1024
 cpu.ram[:]=registered
 (GAME/'logs/sndf-bank-fixture.bin').write_bytes(fixture)
 # Controlled register inputs, not an emulated SPU clock or playback claim.
 cpu.hardware=[];cpu.switches={a+c:0 for a in range(0x1300,0x1c00,0x100) for c in (0,1)}
 commands=[(0x268,[0x005700ae,0,0,0,0x39000]),(0x26a4,[0,45,205]),(0x2f98,[0,2]),(0x2278,[0,0,0xffff])]
 for pc,args in commands:assert cpu.call(pc,args)==0
 voice={'scope':'Original first SFSV batch, register switches explicitly seeded zero; no audio playback emulated',
        'pitch_base':cpu.get(DRIVER+0x24),'unload_cases':unloads,
        'pitch_base_scope':'Correct retail log(1) result; old host float-ABI stub incorrectly sent 1731. See sndf-pitch-probe.json.',
        'commands':[{'pc':hex(pc),'args':args} for pc,args in commands],
        'register_writes':[{'import':hex(a),'entry':hex(x),'value':hex(y)} for a,x,y,_,_ in cpu.hardware],
        'status':cpu.ram[STATUS+0x25cc:STATUS+0x25d0].hex(),
        'pending_key_on':[cpu.get(DRIVER+0xf54),cpu.get(DRIVER+0xf58)],
        'voice':cpu.ram[DRIVER+0x5094:DRIVER+0x50bc].hex()}
 assert voice['status']=='0101007f' and voice['pending_key_on']==[1,0]
 sample_start=struct.unpack_from('<I',banks[0],0x1c)[0]+0x649b0-0x20000
 sample_flags=[];blocks=0
 for pos in range(sample_start,len(banks[0])-15,16):
  blocks+=1;flag=banks[0][pos+1]
  if flag:sample_flags.append({'sample':(blocks-1)*28,'flags':flag})
  if flag&1:break
 assert blocks==812 and sample_flags==[{'sample':22708,'flags':1}]
 voice['first_sample']={'archive_offset':hex(sample_start),'blocks':blocks,'samples':blocks*28,'flags':sample_flags,'scope':'ADPCM block scan only; no envelope, interpolation or audio playback proof'}
 # Original worker a478 calls 3644 after observing zero envelope volume.
 # Exercise that cleanup independently of any invented timer or audio clock.
 started=bytes(cpu.ram)
 assert cpu.call(0x2508,[0])==0
 stopped=cpu.ram[STATUS+0x25cc:STATUS+0x25d0].hex()
 key_off=cpu.get(DRIVER+0xf5c)
 assert stopped=='0102007f' and key_off==1
 cpu.call(0x3644,[0])
 reclaimed=cpu.ram[STATUS+0x25cc:STATUS+0x25d0].hex()
 assert reclaimed=='0000007f' and cpu.get(DRIVER+0x509c,1)==255
 cpu.ram[:]=started
 assert cpu.call(0x25bc,[0])==0
 released=cpu.ram[STATUS+0x25cc:STATUS+0x25d0].hex()
 assert released=='0000007f' and cpu.get(DRIVER+0xf5c)==1
 voice['descriptor_inventory']=[]
 for bank in (banks[0],banks[2]):
  table=struct.unpack_from('<I',bank,0xb4)[0];count=struct.unpack_from('<I',bank,table+4)[0]>>16
  offsets=struct.unpack_from('<I',bank,table+8)[0];groups={}
  for tone in range(count):
   relative=struct.unpack_from('<I',bank,table+offsets+tone*4)[0]
   assert relative and not 0xa9<=relative&255<=0xaf
   descriptor=bank[table+relative:table+relative+28];assert len(descriptor)==28
   key=(descriptor[25],struct.unpack_from('<h',descriptor,12)[0],descriptor[8],struct.unpack_from('<H',descriptor,14)[0],struct.unpack_from('<H',descriptor,16)[0])
   groups[key]=groups.get(key,0)+1
  voice['descriptor_inventory'].append({'bank_id':struct.unpack_from('<I',bank,0x84)[0],'tone_count':count,'groups':[{'curve_index':k[0],'pitch_jitter_cents':k[1],'routing_flags':k[2],'adsr1':k[3],'adsr2':k[4],'tones':n} for k,n in groups.items()]})
 voice['completion_reference']={'stop_status':stopped,'pending_key_off':key_off,
     'reclaimed_status':reclaimed,'explicit_release_status':released,
     'scope':'Original key-off/cleanup routines only. Worker invokes cleanup after zero envelope; no playback completion was simulated.'}
 (GAME/'logs/sndf-voice-probe.json').write_text(json.dumps(voice,indent=2)+'\n')
 return rows


def generate_voice_tables():
 cpu=Sndf()
 text='// Generated from SHA-checked retail sndfi.irx by probe-sndf-init.py.\n'
 for name,address,count in [('SndfPitchTable',0x11d68,1200),('SndfPanTable',0x11960,91)]:
  values=[cpu.get(address+i*2,2) for i in range(count)]
  text+='inline constexpr uint16_t '+name+'['+str(count)+']={\n'
  text+='\n'.join(','.join(str(v) for v in values[i:i+20])+',' for i in range(0,count,20))+'\n};\n'
 (GAME.parents[1]/'ps2xIOP/src/modules/mk_sndf_tables.inc').write_text(text)

def main():
 generate_voice_tables()
 cpu=Sndf();cpu.initialize()
 status=cpu.ram[STATUS:STATUS+0x2b00]
 # Independently describe the reset pattern, checking every byte against original execution.
 expected=bytearray(0x2b00)
 for i in range(48):
  for off,value in ((0x14,255),(0x16,127),(0x17,127),(0x18,64),(0x19,64),(0x1a,127),(0x1b,64)):
   expected[i*16+off]=value
  expected[0x25cf+i*4]=127
 expected[0x30c:0x1b0c]=bytes([255])*0x1800
 assert status==expected, next((hex(i),a,b) for i,(a,b) in enumerate(zip(status,expected)) if a!=b)
 cases=[]
 for name,h,w in [('two_banks',[0x1600,0x2800],[0x1000,0x2200]),('header_overflow',[0x30001],[0]),('wave_overflow',[0],[0x1c7fc1]),('zero',[],[])]:
  cpu=Sndf();cpu.initialize();h=h+[0]*(48-len(h));w=w+[0]*(48-len(w))
  result=cpu.layout(h,w)
  cases.append(dict(name=name,headers=h,waves=w,result=result,ready=cpu.get(DRIVER+0xf48,1),header_bases=[cpu.get(DRIVER+0x3e8+i*4) for i in range(48)],wave_bases=[cpu.get(DRIVER+0x568+i*4) for i in range(48)]))
 cpu=Sndf();cpu.initialize();registration=[]
 def register(label,slot=0):registration.append(dict(name=label,result=cri.signed(cpu.call(0xe1d0,[slot]))))
 register('before_layout');assert cpu.layout([0x1000,0x1000]+[0]*46,[0x1000,0x1000]+[0]*46)==0
 cpu.bank_header(0,magic=False);register('bad_magic')
 cpu.bank_header(0,version=0x4701);register('old_version')
 cpu.bank_header(0);cpu.put(DRIVER+0x16,511,2);register('too_many_waves')
 cpu.put(DRIVER+0x16,0,2);register('success');register('already_registered')
 cpu.bank_header(1);register('second_bank',1)
 out=dict(retail_registration=retail_registration(),irx_sha256=SHA,scope='Original CPU instructions; hardware write imports recorded, no playback/timer claims',reset_status_sha256=hashlib.sha256(status).hexdigest(),layout=cases,registration=registration,passed=True)
 (GAME/'logs/sndf-init-probe.json').write_text(json.dumps(out,indent=2)+'\n')
 fingerprint=14695981039346656037
 for b in status:fingerprint=((fingerprint^b)*1099511628211)&0xffffffffffffffff
 (GAME/'tests/sndf_reset_hash.inc').write_text(str(fingerprint)+'ull\n')
 rows=['// Original sndfi.irx results, generated by probe-sndf-init.py.']
 for c in cases:
  arrays=[c['headers'],c['waves'],c['header_bases'],c['wave_bases']]
  rows.append('{'+str(c['result'])+','+str(c['ready'])+','+','.join('{'+','.join(str(x)+'u' for x in a)+'}' for a in arrays)+'},')
 (GAME/'tests/sndf_layout_cases.inc').write_text('\n'.join(rows)+'\n')
 print('PASS: original reset status byte-for-byte; '+str(len(cases))+' layouts; registrations '+str(registration))
if __name__=='__main__':main()
