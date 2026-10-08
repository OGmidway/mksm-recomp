"""Inspect the first bounded XGKICK failure captured by the runtime."""
import argparse,hashlib,json,pathlib,struct
GAME=pathlib.Path(__file__).resolve().parents[1]
def analyze(raw):
    assert 48<=len(raw)<=48+65536+16384
    h=struct.unpack_from('<12I',raw);magic,version,issue_pc,pc,source,copied,offset,requested,limit,ds,top,itop=h
    assert magic==0x314b4758 and version==1 and copied<=65536 and ds<=16384
    assert len(raw)==48+copied+ds
    packet=raw[48:48+copied];data=raw[48+copied:]
    def tag(b):
        lo,hi=struct.unpack('<QQ',b);n=lo&32767;fmt=(lo>>58)&3;nr=(lo>>60)&15 or 16
        length=16+(n*nr*16 if fmt==0 else ((n*nr+1)//2)*16 if fmt==1 else n*16 if fmt==2 else 0)
        return dict(raw=b.hex(),nloop=n,eop=bool(lo&32768),format=fmt,nreg=nr,registers=hex(hi),bytes=length)
    result=dict(issue_pc=hex(issue_pc),failure_pc=hex(pc),source=hex(source),copied=copied,tag_offset=offset,requested=requested,limit=limit,top=top,itop=itop)
    if offset+16<=len(packet):result['offending_tag']=tag(packet[offset:offset+16])
    if packet:result['first_tag']=tag(packet[:16])
    if ds and ds%16==0:
        addr=(source+offset)%ds;result['current_vu_tag']=tag(data[addr:addr+16])
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');args=ap.parse_args()
    run=json.loads((args.run_dir/'report.json').read_text());raw=(args.run_dir/'xgkick-failure.bin').read_bytes();meta=run['xgkick_failure_capture']
    assert hashlib.sha256(raw).hexdigest()==meta['sha256'] and meta['session']==run['session']
    result=dict(session=run['session'],runner_sha256=run['runner_sha256'],**analyze(raw))
    inputs=args.run_dir/'vu1-starts.bin'
    if inputs.exists() and run.get('vu1_capture'):
        captured=inputs.read_bytes();meta=run['vu1_capture']
        assert hashlib.sha256(captured).hexdigest()==meta['sha256'] and meta['session']==run['session']
        assert len(captured)<=267520
        offset=0;history=[];target=int(result['source'],16)+result['tag_offset'];bad=result.get('offending_tag',{}).get('raw')
        while offset<len(captured):
            assert len(history)<8 and offset+48<=len(captured)
            h=struct.unpack_from('<12I',captured,offset);assert h[0]==0x31555650 and h[1] in (1,2) and h[8]==155
            header=52 if h[1]==2 else 48
            resumed=bool(struct.unpack_from('<I',captured,offset+48)[0]) if h[1]==2 else False
            assert h[5]<=16384 and 16<=h[6]<=16384 and h[6]%16==0
            begin=offset+header+620+h[5];end=begin+h[6];assert end<=len(captured)
            data=captured[begin:end];address=target%h[6]
            history.append(dict(pc=hex(h[2]),resumed=resumed,top=h[3],elapsed_ms=h[11],tag_address=hex(address),tag_raw=data[address:address+16].hex(),already_matches_failure=data[address:address+16].hex()==bad))
            offset=end
        result['preceding_call_inputs']=history
    (GAME/'logs/xgkick-failure-probe.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
