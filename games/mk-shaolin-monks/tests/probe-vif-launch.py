"""Inspect a bounded VIF stream captured at a selected VU launch."""
import argparse,hashlib,json,pathlib,struct
GAME=pathlib.Path(__file__).resolve().parents[1]
NAMES={0:'NOP',1:'STCYCL',2:'OFFSET',3:'BASE',4:'ITOP',5:'STMOD',6:'MSKPATH3',7:'MARK',0x10:'FLUSHE',0x11:'FLUSH',0x13:'FLUSHA',0x14:'MSCAL',0x15:'MSCALF',0x17:'MSCNT',0x20:'STMASK',0x30:'STROW',0x31:'STCOL',0x4a:'MPG',0x50:'DIRECT',0x51:'DIRECTHL'}
def analyze(raw, legacy=False):
    assert 48<=len(raw)<=1048576+56
    h=struct.unpack_from('<12I',raw);assert h[0]==0x31464956 and h[1] in (1,2)
    head=56 if h[1]==2 else 48;assert len(raw)==head+h[2]
    window_start,original_size=struct.unpack_from('<2I',raw,48) if h[1]==2 else (0,h[2])
    data=raw[head:];assert h[3]+4<=len(data)
    pending=0;pending_events=[]
    pos=0;cycle=h[5];cycle_known=False;rows=[];unknown=[];count=0;uncertain=window_start!=0
    while pos+4<=len(data) and pos<=h[3]:
        if legacy and pending:
            chunk=min(pending,(len(data)-pos)//16)
            if not chunk:break
            if len(pending_events)<8:pending_events.append(dict(offset=hex(pos),qwords=chunk))
            pos+=chunk*16;pending-=chunk;continue
        offset=pos;cmd=struct.unpack_from('<I',data,pos)[0];pos+=4;op=(cmd>>24)&127;num=(cmd>>16)&255;imm=cmd&65535;n=num or 256;payload=0
        name=NAMES.get(op,'UNKNOWN')
        if op==1:cycle=imm;cycle_known=True
        elif op==0x20:payload=4
        elif op in (0x30,0x31):payload=16
        elif op==0x4a:payload=n*8
        elif op in (0x50,0x51):payload=imm*16
        elif op&0x60==0x60:
            vn=(op>>2)&3;vl=op&3;name=f'UNPACK V{vn+1}-{(32,16,8,5)[vl]}'
            if vl==3 and vn!=3:uncertain=True
            cl=(cycle&255) or (1 if legacy else 256);wl=((cycle>>8)&255) or (1 if legacy else 256)
            source=n if cl>=wl else (n//wl)*cl+min(n%wl,cl)
            stride=2 if vl==3 and vn==3 else (vn+1)*(32,16,8,16)[vl]//8
            payload=(source*stride+3)&~3
            if not cycle_known:uncertain=True
        elif name=='UNKNOWN':
            if len(unknown)<32:unknown.append(dict(offset=hex(offset),word=hex(cmd)))
        row=dict(offset=hex(offset),word=hex(cmd),name=name,payload_bytes=payload)
        if op in (0x14,0x15):row['raw_pc']=hex(imm*8);row['masked_pc']=hex(imm*8&0x3fff)
        if op&0x60==0x60:row.update(destination=hex(imm&1023),relative_top=bool(imm&32768),vectors=n,cycle=hex(cycle),cycle_known=cycle_known)
        if legacy and op in (0x50,0x51) and payload>=16 and pos+16<=len(data):
            tag=struct.unpack_from('<Q',data,pos)[0]
            if (tag>>58)&3==2:pending=max(0,(tag&32767)-(payload//16-1))
        if pos<=h[3]<pos+payload:row['contains_selected_launch_word']=True
        rows.append(row);rows=rows[-48:];count+=1
        if offset==h[3]:break
        pos+=payload
        if pos>h[3]:uncertain=True;break
    launch=struct.unpack_from('<I',data,h[3])[0]
    return dict(legacy_raw_image_consumption=legacy,pending_raw_reads=pending_events,raw_launch_pc=hex(h[4]),masked_launch_pc=hex(h[4]&0x3fff),stream_bytes=h[2],window_start=window_start,original_stream_bytes=original_size,launch_offset=hex(h[3]),launch_word=hex(launch),commands_seen=count,walk_reached_launch=bool(rows and int(rows[-1]['offset'],16)==h[3]),walk_uncertain=uncertain,unknown_commands=unknown,recent_commands=rows,registers_at_launch=dict(cycle=hex(h[5]),mode=h[6],mask=hex(h[7]),base=h[8],offset=h[9],tops=h[10],itops=h[11]))
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');args=ap.parse_args()
    run=json.loads((args.run_dir/'report.json').read_text());raw=(args.run_dir/'vif-launch.bin').read_bytes();meta=run['vif_launch_capture']
    assert hashlib.sha256(raw).hexdigest()==meta['sha256'] and meta['session']==run['session']
    result=dict(session=run['session'],runner_sha256=run['runner_sha256'],capture_sha256=meta['sha256'],command_boundaries=analyze(raw),legacy_walk=analyze(raw,True))
    (GAME/'logs/vif-launch-probe.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(session=result['session'],legacy_decodes_launch=result['legacy_walk']['walk_reached_launch'],corrected_walk_last_command=result['command_boundaries']['recent_commands'][-1]),indent=2))
if __name__=='__main__':main()
