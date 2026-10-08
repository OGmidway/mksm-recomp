"""Summarize effective post-loading GS triangles from a bounded, hash-checked live capture."""
import argparse,collections,csv,hashlib,itertools,json,math,pathlib,statistics
GAME=pathlib.Path(__file__).resolve().parents[1]

def analyze(rows):
    draws=[list(g) for _,g in itertools.groupby(rows,key=lambda r:r['draw'])]
    assert len(draws)<=4096 and all(len(d)==3 and [int(v['vertex']) for v in d]==[0,1,2] for d in draws)
    states=collections.Counter();boxes=[];oversized=[];degenerate=0;outside=0;nonfinite=0
    for d in draws:
        r=d[0];states[tuple(r[k] for k in ('primitive','fbp','tex','tme','fst','abe','fge','test','zbp','zpsm','zmask'))]+=1
        xy=[(float(v['x'])-int(v['ofx'])/16,float(v['y'])-int(v['ofy'])/16) for v in d]
        box=[min(v[0] for v in xy),min(v[1] for v in xy),max(v[0] for v in xy),max(v[1] for v in xy)];boxes.append(box)
        outside+=box[2]<int(r['scissor_x0']) or box[0]>int(r['scissor_x1']) or box[3]<int(r['scissor_y0']) or box[1]>int(r['scissor_y1'])
        area=(xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-(xy[1][1]-xy[0][1])*(xy[2][0]-xy[0][0])
        degenerate+=abs(area)<0.001
        nonfinite+=any(not math.isfinite(float(v[k])) for v in d for k in ('x','y','z','s','t','q'))
        if box[2]-box[0]>640 or box[3]-box[1]>448:
            if len(oversized)<16:oversized.append(dict(draw=int(r['draw']),box=box,vertices=d))
    return dict(triangles=len(draws),vertices=len(rows),elapsed_ms=([int(rows[0]['elapsed_ms']),int(rows[-1]['elapsed_ms'])] if rows else []),
        degenerate=degenerate,bbox_outside_scissor=outside,nonfinite=nonfinite,
        ranges={k:[min(float(v[k]) for v in rows),statistics.median(float(v[k]) for v in rows),max(float(v[k]) for v in rows)] for k in ('x','y','z','s','t','q','r','g','b','a','fog')} if rows else {},
        states=[dict(state=dict(zip(('primitive','fbp','tex','tme','fst','abe','fge','test','zbp','zpsm','zmask'),s)),count=n) for s,n in states.most_common(32)],oversized_samples=oversized)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');args=ap.parse_args()
    run=json.loads((args.run_dir/'report.json').read_text());path=args.run_dir/'primitives.csv';raw=path.read_bytes()
    assert len(raw)<=16*1024*1024 and hashlib.sha256(raw).hexdigest()==run['primitive_capture']['sha256']
    assert run['primitive_capture']['session']==run['session'] and run['primitive_capture']['triangles_only']
    rows=list(csv.DictReader(raw.decode().splitlines()));assert all(r['primitive'] in ('3','4','5') for r in rows)
    result=dict(session=run['session'],runner_sha256=run['runner_sha256'],note='Actual submitted triangles and effective state, before raster clipping. Bounding-box overlap does not prove visible coverage. No expected camera/geometry reference is assumed.',**analyze(rows))
    (GAME/'logs/scene-triangles-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('oversized_samples','states')},indent=2))
    print('Most common states:',json.dumps(result['states'][:8],indent=2))
if __name__=='__main__':main()
