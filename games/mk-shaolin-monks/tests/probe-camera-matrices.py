"""Compare sampled retail view/projection matrices; no claim of camera-placement correctness."""
import argparse,json,math,pathlib,statistics,struct
GAME=pathlib.Path(__file__).resolve().parents[1]
def multiply(a,b):
    return [sum(a[k*4+row]*b[col*4+k] for k in range(4)) for col in range(4) for row in range(4)]
def examine(watches):
    w={r['name']:struct.unpack('<'+'f'*(len(r['bytes'])//8),bytes.fromhex(r['bytes'])) for r in watches if r['valid']}
    if not all(k in w for k in ('camera','view','projection','viewproj')):return None
    c,v,p,vp=[w[k] for k in ('camera','view','projection','viewproj')]
    if len(c)<3 or any(len(x)!=16 for x in (v,p,vp)):return None
    if not all(math.isfinite(x) for row in (c[:3],v,p,vp) for x in row):return dict(nonfinite=True)
    eye=[sum(v[k*4+i]*c[k] for k in range(3))+v[12+i] for i in range(3)]
    ortho=max(abs(sum(v[k*4+i]*v[k*4+j] for k in range(3))-(1 if i==j else 0)) for i in range(3) for j in range(3))
    expected=multiply(p,v)
    relative=max(abs(a-b)/max(1,abs(a)) for a,b in zip(expected,vp))
    return dict(nonfinite=False,camera=list(c[:3]),eye_origin_max_abs=max(map(abs,eye)),rotation_orthogonality_error=ortho,view_projection_relative_error=relative)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');args=ap.parse_args()
    r=json.loads((args.run_dir/'report.json').read_text());samples=[]
    for s in r['custom_watch_samples']:
        value=examine(s['watches'])
        if value is not None:samples.append(dict(elapsed=s['elapsed'],**value))
    assert samples,'No matching camera watches in report'
    good=[s for s in samples if not s['nonfinite']]
    result=dict(session=r['session'],runner_sha256=r['runner_sha256'],note='Best-effort asynchronous snapshots can catch partially updated matrices. Algebraic agreement does not prove intended camera placement, geometry or VU output.',samples=samples,summary={k:dict(median=statistics.median(s[k] for s in good),maximum=max(s[k] for s in good)) for k in ('eye_origin_max_abs','rotation_orthogonality_error','view_projection_relative_error')} if good else {})
    (GAME/'logs/camera-matrices-probe.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(session=r['session'],samples=len(samples),summary=result['summary']),indent=2))
if __name__=='__main__':main()
