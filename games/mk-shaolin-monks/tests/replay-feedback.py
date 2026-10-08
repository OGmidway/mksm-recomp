"""Replay a bounded CT32 sprite pass; compare cache hypotheses, not hardware proof."""

import argparse, array, collections, csv, hashlib, json, math, pathlib, time

GAME=pathlib.Path(__file__).resolve().parents[1]

RUN=GAME/'logs/first-stage-relinked'

BLOCK=((0,1,4,5,16,17,20,21),(2,3,6,7,18,19,22,23),(8,9,12,13,24,25,28,29),(10,11,14,15,26,27,30,31))

COL=((0,1,4,5,8,9,12,13),(2,3,6,7,10,11,14,15),(16,17,20,21,24,25,28,29),(18,19,22,23,26,27,30,31),(32,33,36,37,40,41,44,45),(34,35,38,39,42,43,46,47),(48,49,52,53,56,57,60,61),(50,51,54,55,58,59,62,63))

def addr(base,bw,x,y):

    page=(base>>5)+(y>>5)*max(bw,1)+(x>>6)

    block=(base&31)+BLOCK[(y>>3)&3][(x>>3)&7]

    return ((page<<11)+(block>>5)*2048+(block&31)*64+COL[y&7][x&7])&0xfffff



def clamp(v):return max(0,min(255,v))

def channels(v):return [(v>>s)&255 for s in (0,8,16,24)]

def pack(c):return sum(v<<s for v,s in zip(c,(0,8,16,24)))



def replay(initial,draws,mode):

    mem=array.array('I');mem.frombytes(initial)

    cache_page=-1;cache=None;last_flush=None;writes=0;begin=time.monotonic()

    def texread(a):

        nonlocal cache_page,cache

        if mode=='page':

            page=a>>11

            if page!=cache_page:

                cache_page=page;cache=mem[page*2048:(page+1)*2048]

            return cache[a&2047]

        return (mem if mode=='coherent' else cache)[a]

    for vertices in draws:

        a,b=vertices;d=a

        assert not d['fge'] and not d['pabe'] and d['colclamp']==1 and d['zmask']==1

        assert ((d['test']>>17)&3)==1 and not (d['test']&(1<<14))

        assert not (d['test']&1) or (d['test']&0x300f)==0x1001, 'unsupported alpha test'

        if d['flush']!=last_flush:

            cache_page=-1

            if mode=='flush':cache=mem[:]

            last_flush=d['flush']

        if mode=='draw':cache=mem[:]

        x0,x1=sorted((int(a['x'])-(d['ofx']>>4),int(b['x'])-(d['ofx']>>4)))

        y0,y1=sorted((int(a['y'])-(d['ofy']>>4),int(b['y'])-(d['ofy']>>4)))

        width=max(1,x1-x0);height=max(1,y1-y0)

        xlo=max(x0,d['sx0']);xhi=min(x0+width-1,d['sx1']);ylo=max(y0,d['sy0']);yhi=min(y0+height-1,d['sy1'])

        vc=[b[k] for k in ('r','g','b','a')]

        alpha=d['alpha'];asel=alpha&3;bsel=(alpha>>2)&3;csel=(alpha>>4)&3;dsel=(alpha>>6)&3;fix=(alpha>>32)&255

        assert d['clamp']==5, 'current experiment requires clamp mode'

        uw,vh=1<<d['tw'],1<<d['th']

        for y in range(ylo,yhi+1):

            tv=(a['v']>>4)+((b['v']>>4)-(a['v']>>4))*((y-y0+.5)/height)

            tv=max(0,min(65535,int(tv*16+.5)))/16

            for x in range(xlo,xhi+1):

                color=vc[:]

                if d['tme']:

                    assert d['fst'] and d['tfx'] in (0,1)

                    tu=(a['u']>>4)+((b['u']>>4)-(a['u']>>4))*((x-x0+.5)/width)

                    tu=max(0,min(65535,int(tu*16+.5)))/16

                    def sample(u,v):return channels(texread(addr(d['tex'],d['tbw'],max(0,min(uw-1,u)),max(0,min(vh-1,v)))))

                    if d['linear']:

                        fu,fv=tu-.5,tv-.5;u,v=math.floor(fu),math.floor(fv);fx,fy=fu-u,fv-v

                        c00,c10,c01,c11=sample(u,v),sample(u+1,v),sample(u,v+1),sample(u+1,v+1)

                        color=[]

                        for k in range(4):

                            top=c00[k]+(c10[k]-c00[k])*fx;bottom=c01[k]+(c11[k]-c01[k])*fx

                            color.append(clamp(math.floor(top+(bottom-top)*fy+.5)))

                    else:color=sample(int(tu),int(tv))

                    if d['tfx']==0:color=[clamp((color[k]*vc[k])>>7) for k in range(4)]

                    if not d['tcc']:color[3]=vc[3]

                dest_addr=addr(d['fbp']*32,d['fbw'],x,y);old=mem[dest_addr];dc=channels(old)

                if d['abe']:

                    factor=color[3] if csel==0 else dc[3] if csel==1 else fix

                    for k in range(3):

                        choices=(color[k],dc[k],0,0)

                        color[k]=clamp(((choices[asel]-choices[bsel])*factor>>7)+choices[dsel])

                if d['fba']&1:color[3]|=128

                value=pack(color);mask=d['fbmsk'];mem[dest_addr]=(value&(~mask&0xffffffff))|(old&mask);writes+=1

    return mem,writes,round(time.monotonic()-begin,3)



def main():
    global RUN

    ap=argparse.ArgumentParser();ap.add_argument('--mode',choices=['coherent','page','draw','flush'],default='coherent');ap.add_argument('--run-dir',type=pathlib.Path,default=RUN);args=ap.parse_args();RUN=args.run_dir

    report=json.loads((RUN/'report.json').read_text());capture=report['feedback_capture'];assert capture['session']==report['session']

    data={suffix:(RUN/('feedback'+suffix)).read_bytes() for suffix in ('.csv','.start.bin','.end.bin')}

    for suffix,blob in data.items():assert hashlib.sha256(blob).hexdigest()==capture['files'][suffix]

    assert len(data['.start.bin'])==len(data['.end.bin'])==4194304

    grouped=collections.OrderedDict()

    for row in csv.DictReader(data['.csv'].decode().splitlines()):

        row={k:float(v) if k in ('x','y','z') else int(v) for k,v in row.items()};grouped.setdefault(row['draw'],[]).append(row)

    assert 0<len(grouped)<=4096 and all(len(v)==2 for v in grouped.values())

    out,writes,seconds=replay(data['.start.bin'],list(grouped.values()),args.mode)

    expected=array.array('I');expected.frombytes(data['.end.bin'])

    mismatches=sum(a!=b for a,b in zip(out,expected));first=next((i for i,(a,b) in enumerate(zip(out,expected)) if a!=b),None)

    result={'session':report['session'],'runner_sha256':report['runner_sha256'],'mode':args.mode,'draws':len(grouped),'flush_values':sorted({d[0]['flush'] for d in grouped.values()}),'pixel_writes':writes,'seconds':seconds,'mismatched_words':mismatches,'first_mismatch_word':first,'first_actual':None if first is None else hex(out[first]),'first_expected':None if first is None else hex(expected[first]),'white_words':sum((v&0xffffff)==0xffffff for v in out),'limitation':'Cache modes are controlled hypotheses, not a complete PS2 texture cache model.'}

    path=GAME/'logs/feedback-replay-report.json';history=json.loads(path.read_text()) if path.exists() else {}

    if history.get('session')!=report['session']:history={'session':report['session'],'results':{}}

    history['results'][args.mode]=result;path.write_text(json.dumps(history,indent=2));print(json.dumps(result,indent=2))

if __name__=='__main__':main()

