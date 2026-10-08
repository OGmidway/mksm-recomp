import pathlib,struct,re,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
b=(root.parent.parent/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
assert hashlib.sha256(b).hexdigest()=='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
p=struct.unpack_from('<I',b,28)[0];off,va=struct.unpack_from('<II',b,p+4)
def word(a):return struct.unpack_from('<I',b,a-va+off)[0]
registered=set(int(x,16) for x in re.findall(r'// (0x[0-9a-f]+)',(root/'runtime/output/register_functions.cpp').read_text()))
rows=(root/'config/kernel-functions.csv').read_text().splitlines()
registered.update(int(x.split(',')[1],16) for x in rows[1:])
a=0x5656e0; added=[]
while word(a):
 t=word(a);a+=4
 if t in registered:continue
 w=[word(t+i) for i in range(0,40,4)]
 if w[:4]==[0x27bdfff0,0x24040001,0xffbf0000,0x3405ffff] and w[4]>>26==3 and w[5:]==[0,0xdfbf0000,0x27bd0010,0x03e00008,0]:size=40
 elif w[:3]==[0x27bdfff0,0x24040001,0xffbf0000] and w[3]>>26==3 and w[4:8]==[0x3405ffff,0xdfbf0000,0x03e00008,0x27bd0010]:size=32
 else:raise RuntimeError(f'Unrecognized constructor {t:x}: {w}')
 added.append(f'mk_ctor_{t:08x},0x{t:08X},0x{t+size:08X},{size}')
(root/'config/kernel-functions.csv').write_text('\n'.join(rows+added)+'\n')
print(f'Added {len(added)} verified constructors; table ends at {a-4:#x}')
