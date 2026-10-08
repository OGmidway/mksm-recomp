"""Inspect bounded VU1 execute inputs; does not claim complete replay or correct geometry."""
import argparse,hashlib,json,math,pathlib,struct
GAME=pathlib.Path(__file__).resolve().parents[1]

def parse(raw):
    assert len(raw)<=8*(52+620+32768),'capture exceeds eight-record byte cap'
    records=[];offset=0
    while offset<len(raw):
        assert len(records)<8 and offset+48<=len(raw),'record cap or truncated header'
        h=struct.unpack_from('<12I',raw,offset);offset+=48
        magic,version,pc,top,itop,cs,ds,budget,words,db,tb,elapsed=h
        assert magic==0x31555650 and version in (1,2) and words==155
        resumed=False
        if version==2:
            assert offset+4<=len(raw),'truncated resume flag'
            flag=struct.unpack_from('<I',raw,offset)[0];offset+=4
            assert flag in (0,1);resumed=bool(flag)
        assert cs<=16384 and ds<=16384 and pc%8==0 and pc<cs
        assert offset+620+cs+ds<=len(raw),'truncated state/code/data'
        state=struct.unpack_from('<155I',raw,offset);offset+=620
        code=raw[offset:offset+cs];offset+=cs;data=raw[offset:offset+ds];offset+=ds
        floats=struct.unpack('<128f',struct.pack('<128I',*state[:128]))
        preview=[]
        for a in range(pc,min(pc+8*32,cs),8):
            lower,upper=struct.unpack_from('<II',code,a)
            preview.append(dict(pc=hex(a),lower=hex(lower),upper=hex(upper),end=bool(upper&0x40000000),immediate=bool(upper&0x80000000)))
        records.append(dict(resumed=resumed,pc=hex(pc),top=top,itop=itop,elapsed_ms=elapsed,cycle_budget=budget,
            code_sha256=hashlib.sha256(code).hexdigest(),data_sha256=hashlib.sha256(data).hexdigest(),
            vf0_bits=[hex(x) for x in state[:4]],nonfinite_vf=sum(not math.isfinite(x) for x in floats),
            vf=[list(floats[i:i+4]) for i in range(0,128,4)],vi=list(state[128:144]),
            entry_instructions=preview,code_bytes=cs,data_bytes=ds,debug_bits=[db,tb]))
    return records

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');ap.add_argument('--self-test',action='store_true');args=ap.parse_args()
    if args.self_test:
        header=struct.pack('<12I',0x31555650,1,0,0,0,8,16,100,155,0,0,300000)
        record=header+bytes(620+8+16)
        assert len(parse(record*8))==8
        h2=struct.pack('<13I',0x31555650,2,0,0,0,8,16,100,155,0,0,300000,1)
        assert parse(h2+bytes(620+8+16))[0]['resumed']
        for broken in (record[:-1],record*9,bytes(48)):
            try:parse(broken)
            except AssertionError:pass
            else:raise AssertionError('malformed capture accepted')
        print('PASS valid eight records; reject truncation, record overflow, bad magic');return
    run=json.loads((args.run_dir/'report.json').read_text());meta=run['vu1_capture'];raw=(args.run_dir/'vu1-starts.bin').read_bytes()
    assert hashlib.sha256(raw).hexdigest()==meta['sha256'] and meta['session']==run['session']
    records=parse(raw)
    report=dict(session=run['session'],runner_sha256=run['runner_sha256'],note='Up to eight execute/resume inputs after delay, optionally distinct PCs. Pipeline queues, GS memory and expected scene geometry are not captured. Nonfinite VF lanes may be packed integer data.',records=records)
    (GAME/'logs/vu1-starts-probe.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(session=run['session'],records=[{k:r[k] for k in ('resumed','pc','top','itop','elapsed_ms','code_sha256','data_sha256','vf0_bits','nonfinite_vf')} for r in records]),indent=2))
if __name__=='__main__':main()
