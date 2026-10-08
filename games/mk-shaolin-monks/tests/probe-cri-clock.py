"""Replay the retail video timing gate using an inspector capture, without guest writes.

Only executes the bounded integer/FPU instruction subset used by 0x451c68.
Counterfactual clocks are investigation cases, not replacement runtime behavior.
"""
import json, pathlib, runpy, struct
GAME=pathlib.Path(__file__).resolve().parents[1]
S=runpy.run_path(str(GAME/'tests/transfer-prototype-symbols.py'))
MASK=(1<<64)-1

def signed(x,bits=32):
    x&=(1<<bits)-1
    return x-(1<<bits) if x>>(bits-1) else x

def bits(f): return struct.unpack('<I',struct.pack('<f',f))[0]
def floating(x): return struct.unpack('<f',struct.pack('<I',x&0xffffffff))[0]

def execute(read,mem,video_time,audio_time):
    ram=dict(mem); r=[0]*32; f=[0]*32
    r[4]=0x100000; r[5]=0x80000; r[31]=0x90000
    f[12]=bits(video_time); f[13]=bits(audio_time)
    pc=0x451c68; pending=None; condition=False; visited=[]
    for _ in range(150):
        if pc==0x90000:
            return ram[r[5]],visited
        visited.append(hex(pc))
        w=struct.unpack('<I',read(pc,4))[0]
        op=w>>26;rs=w>>21&31;rt=w>>16&31;rd=w>>11&31;sa=w>>6&31;fn=w&63
        imm=w&65535;si=signed(imm,16);old=pending;pending=None;nxt=pc+4
        if w==0:pass
        elif op==15:r[rt]=signed(imm<<16)
        elif op==9:r[rt]=signed(r[rs]+si)
        elif op==10:r[rt]=int(signed(r[rs],64)<si)
        elif op==11:r[rt]=int(r[rs]<(si&MASK))
        elif op==13:r[rt]=r[rs]|imm
        elif op==14:r[rt]=r[rs]^imm
        elif op in (35,49):
            value=ram.get((r[rs]+si)&0xffffffff,0)
            if op==35:r[rt]=signed(value)
            else:f[rt]=value
        elif op in (43,57):ram[(r[rs]+si)&0xffffffff]=(r[rt] if op==43 else f[rt])&0xffffffff
        elif op==0 and fn==45:r[rd]=r[rs]+r[rt]
        elif op==0 and fn==42:r[rd]=int(signed(r[rs],64)<signed(r[rt],64))
        elif op==0 and fn==8:pending=r[rs]&0xffffffff
        elif op in (4,5,20):
            taken=(r[rs]==r[rt])==(op!=5)
            if taken:pending=pc+4+si*4
            elif op==20:nxt=pc+8
        elif op==17:
            if rs==8:
                taken=condition==bool(rt&1)
                if taken:pending=pc+4+si*4
                elif rt&2:nxt=pc+8
            elif rs==20 and fn==32:f[sa]=bits(float(signed(f[rd])))
            elif rs==16 and fn in (0,1):
                a,b=floating(f[rd]),floating(f[rt]);f[sa]=bits(a+b if fn==0 else a-b)
            elif rs==16 and fn in (50,52,54):
                a,b=floating(f[rd]),floating(f[rt]);condition=(a==b if fn==50 else a<b if fn==52 else a<=b)
            else:raise ValueError(f'Unsupported COP1 {w:08x} at {pc:x}')
        else:raise ValueError(f'Unsupported {w:08x} at {pc:x}')
        r=[x&MASK for x in r];r[0]=0;pc=old if old is not None else nxt
    raise ValueError('Instruction limit')

def main():
    read=S['image'](S['ROOT']/'MortalKombatShaolinMonks/SLUS_210.87',S['HASHES']['retail'])
    capture=json.loads((GAME/'logs/inspector/inspector.json').read_text())
    report=json.loads((GAME/'logs/inspector/report.json').read_text())
    assert report['elf_sha256']==S['HASHES']['retail']
    assert capture['session']==report['session'] and capture['pid']==report['pid']
    watches={w['name']:w for w in capture['watches'] if w['valid']}
    movie=watches.get('movie_header')
    codec_base=struct.unpack_from('<I',bytes.fromhex(movie['bytes']),0x3c)[0] if movie else 0x105c3c0
    assert 0<codec_base<0x2000000
    mem={}
    for name in ('movie_time','movie_config'):
        w=watches[name];data=bytes.fromhex(w['bytes'])
        for i in range(0,len(data),4):mem[0x100000+w['address']-codec_base+i]=struct.unpack_from('<I',data,i)[0]
    assert mem[0x100a44]==0 and mem[0x100a48]!=1 and mem[0x100abc]==0
    video_count,video_rate=mem[0x100fac],mem[0x100fb0]
    audio_count,audio_rate=mem[0x100fbc],mem[0x100fc0]
    assert video_count>0 and video_rate>0 and audio_rate>0
    video_time=floating(bits(floating(bits(video_count*10000.0))/video_rate))
    audio_time=floating(bits(floating(bits(audio_count*10000.0))/audio_rate))
    tolerance=floating(bits(float(signed(mem[0x100ac4]))))
    # The near-clock branch also reads +920 and global 55f9c0, neither captured
    # by these watches. Do not silently substitute zero and call it live proof.
    outside_window=(floating(bits(audio_time+tolerance))<video_time or
                    video_time<=floating(bits(audio_time-tolerance)))
    live={'audio_samples':audio_count,'video_time_100us':video_time,
          'audio_time_100us':audio_time,'replayed':outside_window}
    if outside_window:
        ready,path=execute(read,mem,video_time,audio_time)
        assert '0x451cdc' not in path
        live.update(picture_ready=ready,path=path)
    else:
        live['reason']='Near-clock branch needs uncaptured +920 and global 55f9c0'
    # Preserve the original zero-clock regression independently of the latest
    # timestamp: a moving movie can be far beyond the old 2000-sample example.
    fixture_mem=dict(mem);fixture_mem[0x100ac4]=41
    fixture_video=floating(bits(floating(bits(1000*10000.0))/29970))
    cases=[]
    for count in (0,2000,48000):
        fixture_audio=floating(bits(floating(bits(count*10000.0))/48000))
        result,path=execute(read,fixture_mem,fixture_video,fixture_audio)
        cases.append({'audio_samples':count,'picture_ready':result,'path':path})
    assert [c['picture_ready'] for c in cases]==[0,1,1]
    assert '0x451c84' in cases[0]['path']
    result={'scope':'Captured timing gate plus historical zero-clock regression; no guest writes',
            'elf_sha256':S['HASHES']['retail'],'session':capture['session'],'pid':capture['pid'],
            'codec_base':hex(codec_base),'runner_sha256':report['runner_sha256'],'captured_unix_ms':capture['captured_unix_ms'],
            'video_count':video_count,'video_rate':video_rate,'audio_count':audio_count,'audio_rate':audio_rate,
            'tolerance_100us':mem[0x100ac4],'live':live,
            'historical_fixture':{'video_count':1000,'video_rate':29970,'audio_rate':48000,
                                  'tolerance_100us':41,'cases':cases},'passed':True}
    (GAME/'logs/cri-clock-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS: historical zero-clock gate regression; captured audio={audio_count}/{audio_rate}, live replay={outside_window}')
if __name__=='__main__':main()

