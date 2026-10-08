"""Inspect capped native PCM against an independent decode of the retail intro. No playback."""
import collections, cmath, hashlib, json, math, struct, subprocess
from pathlib import Path
GAME=Path(__file__).resolve().parents[1]; ROOT=GAME.parents[1]; RUN=GAME/'logs/inspector'
def fft(a):
    if len(a)==1:return a
    e,o=fft(a[::2]),fft(a[1::2]);t=[cmath.exp(-2j*math.pi*k/len(a))*o[k] for k in range(len(a)//2)]
    return [x+y for x,y in zip(e,t)]+[x-y for x,y in zip(e,t)]
def stats(v,rate):
    windows=[]
    for offset in range(0,len(v)//2-4096,rate//2):
        x=v[offset*2:(offset+4096)*2:2]
        spectrum=[abs(a)**2 for a in fft([complex(s*(.5-.5*math.cos(2*math.pi*i/4095))) for i,s in enumerate(x)])[:2049]]
        power=sum(spectrum);peak=max(range(1,len(spectrum)),key=spectrum.__getitem__)
        windows.append(dict(seconds=offset/rate,peak_hz=peak*rate/4096,peak_fraction=spectrum[peak]/power if power else 0))
    return dict(frames=len(v)//2,sample_rate=rate,minimum=min(v),maximum=max(v),
        rms=math.sqrt(sum(s*s for s in v)/len(v)),clipped_fraction=sum(abs(s)>=32760 for s in v)/len(v),windows=windows)
def main():
    report=json.loads((RUN/'report.json').read_text());capture=report['pcm_capture']
    assert not report['debug_skip_intro'],'Reference is the intro, not frontend music'
    b=Path(capture['path']).read_bytes();assert hashlib.sha256(b).hexdigest()==capture['sha256']
    assert capture['session']==report['session'];assert len(b)<=2*1024*1024
    streams=collections.defaultdict(list);pos=0
    while pos<len(b):
        magic,key,rate,n=struct.unpack_from('<4I',b,pos);assert magic==0x434d4350 and 0<n<=4096
        end=pos+16+4*n;assert end<=len(b)
        streams[key,rate].extend(struct.unpack_from('<'+'h'*(n*2),b,pos+16));pos=end
    asset=ROOT/'MortalKombatShaolinMonks/Front/Movies/midway.sfd'
    ff=ROOT/'out/mk-runtime/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin/ffmpeg.exe'
    proc=subprocess.run([str(ff),'-v','error','-i',str(asset),'-map','0:a:0','-t','5','-ar','48000','-ac','2','-f','s16le','-'],capture_output=True,timeout=30)
    assert proc.returncode==0,proc.stderr.decode();ref=struct.unpack('<'+'h'*(len(proc.stdout)//2),proc.stdout)
    result=dict(session=report['session'],runner_sha256=report['runner_sha256'],capture_sha256=capture['sha256'],
        mute_confirmed=report['mute_confirmed'],asset_sha256=hashlib.sha256(asset.read_bytes()).hexdigest(),
        reference=stats(ref,48000),live=[dict(key=hex(key),**stats(v,rate)) for (key,rate),v in streams.items()],
        scope='Pre-device submitted PCM; windows are not time-aligned to the reference. Clipping/tone analysis does not identify the faulty instruction or establish audible fidelity.')
    (GAME/'logs/live-pcm-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('reference','live')}))
    for name,entry in [('reference',result['reference'])]+[('live',v) for v in result['live']]:
        print(name,'rms',round(entry['rms'],2),'clipped',round(entry['clipped_fraction'],5),'first tone',entry['windows'][:1])
if __name__=='__main__':main()
