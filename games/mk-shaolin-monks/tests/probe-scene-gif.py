"""Inspect bounded live GIF geometry without changing guest memory or renderer state."""
import argparse, collections, hashlib, json, math, pathlib, struct
GAME=pathlib.Path(__file__).resolve().parents[1]

def inspect(raw):
    formats=collections.Counter(); registers=collections.Counter(); vertices=[]; writes=[]; partial=[]
    at=0; packet_count=0; stq=None; rgba=None
    while at<len(raw):
        size,=struct.unpack_from('<I',raw,at);at+=4
        assert 16<=size<=8*1024*1024 and at+size<=len(raw)
        packet=raw[at:at+size];at+=size;offset=0;packet_count+=1
        assert packet_count<=512
        while offset+16<=len(packet):
            lo,hi=struct.unpack_from('<QQ',packet,offset);offset+=16
            loops=lo&32767;fmt=(lo>>58)&3;count=(lo>>60) or 16
            rr=[(hi>>(4*i))&15 for i in range(count)]
            length=loops*count*16 if fmt==0 else ((loops*count+1)//2)*16 if fmt==1 else loops*16
            if offset+length>len(packet):
                partial.append(dict(packet=packet_count,format=fmt,expected=length,available=len(packet)-offset))
                break
            formats[fmt]+=1
            stq=None;rgba=None  # Only report state explicitly supplied by this tag.
            if fmt==0:
                if lo&(1<<46) and len(writes)<96:
                    writes.append(dict(packet=packet_count,register='PRIM/PRE',value=hex((lo>>47)&2047)))
                for i in range(loops*count):
                    a,b=struct.unpack_from('<QQ',packet,offset+16*i);reg=rr[i%count];registers[reg]+=1
                    if reg==1:rgba=[a&255,(a>>32)&255,b&255,(b>>32)&255]
                    elif reg==2:
                        vals=struct.unpack('<3f',struct.pack('<QI',a,b&0xffffffff))
                        stq=[v if math.isfinite(v) else str(v) for v in vals]
                    elif reg in (4,5,12,13):
                        fog=(b>>36)&255 if reg in (4,12) else None
                        z=(b>>4)&0xffffff if reg in (4,12) else b&0xffffffff
                        vertices.append(dict(packet=packet_count,register=reg,x=(a&65535)/16,y=((a>>32)&65535)/16,
                            z=z,fog=fog,adc=bool((b>>47)&1),stq=stq,rgba=rgba))
                    elif reg==14 and len(writes)<96:
                        writes.append(dict(packet=packet_count,register=hex(b&255),value=hex(a)))
            offset+=length
    return dict(packets=packet_count,partial_payloads=partial,tag_formats=dict(formats),packed_register_counts=dict(registers),
        vertex_count=len(vertices),vertex_bounds=({k:[min(v[k] for v in vertices),max(v[k] for v in vertices)] for k in ('x','y','z')} if vertices else {}),
        fog_counts=dict(collections.Counter(v['fog'] for v in vertices if v['fog'] is not None)),
        rgba_counts=dict(collections.Counter(str(v['rgba']) for v in vertices).most_common(16)),
        vertex_samples=vertices[:96],geometry_samples=[v for v in vertices if v["register"] in (4,12)][:96],register_samples=writes)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');args=parser.parse_args()
    run=json.loads((args.run_dir/'report.json').read_text());raw=(args.run_dir/'gif.bin').read_bytes()
    assert len(raw)<=8*1024*1024+512*4
    assert run['gif_capture']['session']==run['session'] and run['gif_capture']['sha256']==hashlib.sha256(raw).hexdigest()
    result=dict(session=run['session'],runner_sha256=run['runner_sha256'],note='Raw packed vertices only; coordinates still include XYOFFSET. Capture may start mid-frame. Register-list draws and initial GS state are not reconstructed; this does not establish correct rendering.',**inspect(raw))
    (GAME/'logs/scene-gif-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('vertex_samples','geometry_samples','register_samples')},indent=2))
if __name__=='__main__':main()
