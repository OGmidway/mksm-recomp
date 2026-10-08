"""Summarize all bounded scene draws, including sprites and framebuffer clears."""
import argparse,collections,csv,hashlib,itertools,json,pathlib
GAME=pathlib.Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');args=ap.parse_args()
    run=json.loads((args.run_dir/'report.json').read_text());raw=(args.run_dir/'primitives.csv').read_bytes();meta=run['primitive_capture']
    assert len(raw)<=16*1024*1024 and hashlib.sha256(raw).hexdigest()==meta['sha256'] and meta['session']==run['session']
    rows=list(csv.DictReader(raw.decode().splitlines()));draws=[list(g) for _,g in itertools.groupby(rows,key=lambda r:r['draw'])];assert len(draws)<=4096
    primitive_counts=collections.Counter();states=collections.Counter();examples={}
    for d in draws:
        r=d[0];kind=int(r['primitive']);primitive_counts[kind]+=1
        xy=[(float(v['x'])-int(v['ofx'])/16,float(v['y'])-int(v['ofy'])/16) for v in d]
        box=[min(x for x,y in xy),min(y for x,y in xy),max(x for x,y in xy),max(y for x,y in xy)]
        if kind!=6:continue
        keys=('fbp','tex','tme','fst','abe','test','alpha','zbp','zmask','fbmsk','r','g','b','a')
        key=tuple(r[k] for k in keys);states[key]+=1
        if key not in examples:examples[key]=dict(draw=int(r['draw']),state=dict(zip(keys,key)),box=box,vertices=d)
    result=dict(session=run['session'],runner_sha256=run['runner_sha256'],primitive_capture_sha256=meta['sha256'],draws=len(draws),primitive_counts=dict(primitive_counts),elapsed_ms=[rows[0]['elapsed_ms'],rows[-1]['elapsed_ms']] if rows else [],sprite_states=[dict(count=n,**examples[k]) for k,n in states.most_common(32)],note='Submitted draw state before clipping. A bounding box does not establish pixel ownership.')
    (GAME/'logs/scene-draws-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='sprite_states'},indent=2))
    print(json.dumps([dict(count=v['count'],state=v['state'],box=v['box']) for v in result['sprite_states']],indent=2))
if __name__=='__main__':main()
