"""Read-only candidate discovery for MKSM's observed seven-method frontend tables.

The layout is established by live objects at 573478 and 573988. Candidates
still require Ghidra boundary review before adding translations. No guest writes.
"""
from pathlib import Path
import csv, hashlib, json, struct
GAME=Path(__file__).resolve().parents[1];ROOT=GAME.parents[1]
SHA='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
def main():
    blob=(ROOT/'MortalKombatShaolinMonks/SLUS_210.87').read_bytes()
    assert hashlib.sha256(blob).hexdigest()==SHA
    ph=struct.unpack_from('<I',blob,28)[0];entry,count=struct.unpack_from('<HH',blob,42)
    segments=[]
    for i in range(count):
        kind,offset,address,_,size=struct.unpack_from('<5I',blob,ph+i*entry)
        if kind==1:segments.append((address,offset,size))
    def read(address,size):
        for start,offset,length in segments:
            if start<=address and address+size<=start+length:
                return blob[offset+address-start:offset+address-start+size]
        raise ValueError(hex(address))
    with (GAME/'config/kernel-functions.csv').open() as source:
        known={int(row['start'],16):row for row in csv.DictReader(source)}
    tables=[]
    for address in range(0x572e00,0x573a00,8):
        words=struct.unpack('<18I',read(address,72));slots=words[3:16:2]
        if (words[:3]==(0,0,0) and words[16:]==(0,0) and
            all(v==0 for v in words[4:16:2]) and all(0x100000<=v<0x4b2000 and v%4==0 for v in slots)):
            tables.append(dict(address=hex(address),methods=[dict(address=hex(v),in_supplement=v in known,
                verified_end=known[v]['end'] if v in known else None) for v in slots]))
    report=dict(elf_sha256=SHA,region=['0x572e00','0x573a00'],tables=tables,
        scope='Candidate layout discovery and reviewed-map coverage, not proof of execution or gameplay')
    (GAME/'logs/frontend-vtables.json').write_text(json.dumps(report,indent=2)+'\n')
    missing=sorted({method['address'] for table in tables for method in table['methods'] if not method['in_supplement']})
    print('Observed-layout candidates:',len(tables),'outside reviewed supplement:',missing)
if __name__=='__main__':main()
