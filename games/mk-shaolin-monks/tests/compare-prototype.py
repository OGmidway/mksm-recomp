import pathlib,struct,hashlib,re,json,csv
root=pathlib.Path(__file__).resolve().parents[3];proto=root/'MKSMprototype';retail=root/'MortalKombatShaolinMonks/SLUS_210.87'
def elf(path):
 d=path.read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',d);heads=[struct.unpack_from('<IIIIIIII',d,h[5]+i*h[9]) for i in range(h[10])]
 sections=[struct.unpack_from('<IIIIIIIIII',d,h[6]+i*h[11]) for i in range(h[12])];st=sections[h[13]];names=d[st[4]:st[4]+st[5]]
 result=[]
 for s in sections:result.append({'name':names[s[0]:].split(b'\0')[0].decode(),'address':s[3],'offset':s[4],'size':s[5],'type':s[1]})
 return d,heads,result
def read(image,address,size):
 d,heads,_=image
 for h in heads:
  if h[0]==1 and h[2]<=address and address+size<=h[2]+h[4]:return d[h[1]+address-h[2]:h[1]+address-h[2]+size]
 raise ValueError(hex(address))
def norm(w):
 op=w>>26;base=(w>>21)&31
 if op in (2,3):return w&0xfc000000
 if op==15:return w&0xffff0000
 # Candidate matching only: mask relocation-sensitive immediates, retain SP and branch offsets.
 if (op in (9,13,25) or 32<=op<=63) and base!=29:return w&0xffff0000
 return w
p=elf(proto/'SLUS_210.87');r=elf(retail);j=elf(proto/'jarvos.elf')
text=next(s for s in p[2] if s['name']=='.text');raw=read(p,text['address'],text['size']);normalized=b''.join(struct.pack('<I',norm(w[0])) for w in struct.iter_unpack('<I',raw))
maprows=[]
for line in (proto/'mka.map').read_text(errors='replace').splitlines():
 m=re.match(r'^([0-9a-fA-F]{8}) ([0-9a-fA-F]{8})\s+0\s{8,}(\S.*)$',line)
 if m and int(m[2],16):maprows.append({'address':int(m[1],16),'size':int(m[2],16),'name':m[3]})
byaddr={row['address']:row for row in maprows}
retailrows={int(x['Start'],16):x for x in csv.DictReader((root/'games/mk-shaolin-monks/ghidra/functions.csv').open())}
report={'prototype_sha256':hashlib.sha256(p[0]).hexdigest(),'map_sha256':hashlib.sha256((proto/'mka.map').read_bytes()).hexdigest(),'prototype_sections':p[2],'jarvos_debug_sections':[s for s in j[2] if 'debug' in s['name'] or s['type']==2],'map_nonzero_symbols':len(maprows),'candidates':[],'map_exact_build_verified':False}
for target in [0x20ecd0,0x20f058,0x212f70,0x378a30,0x476988,0x47c2a8,0x480530,0x486c10]:
 row=retailrows.get(target);n=min(64,(int(row['End'],16)-target)//4) if row else 32
 body=read(r,target,n*4);sig=b''.join(struct.pack('<I',norm(w[0])) for w in struct.iter_unpack('<I',body))
 hits=[];pos=normalized.find(sig)
 while pos>=0:
  if pos%4==0:
   a=text['address']+pos;near=[v for v in maprows if abs(v['address']-a)<=0x200]
   near.sort(key=lambda x:abs(x['address']-a))
   hits.append({'prototype_address':hex(a),'exact_map_symbol':byaddr.get(a),'nearest_map_candidates':near[:2]})
  pos=normalized.find(sig,pos+1)
 report['candidates'].append({'retail_address':hex(target),'prefix_instructions':n,'matches':hits[:10],'match_count':len(hits)})
print('map symbols',len(maprows))
for row in report['candidates']:print(json.dumps(row))
out=root/'games/mk-shaolin-monks/ghidra/prototype-symbols.json';out.write_text(json.dumps(report,indent=2))
