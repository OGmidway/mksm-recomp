"""Original IOP equal-rate PCM conversion, before host streaming integration."""
import importlib.util,json,struct
from pathlib import Path
p=Path(__file__).with_name('probe-cri-queue.py')
spec=importlib.util.spec_from_file_location('queue_probe',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
cpu=m.Iop(1,3)
obj,source,dest,nin,nout=0x24000,0x25000,0x27000,0x29000,0x29004
cases=[]
for incoming,room in ((32,32),(64,32),(32,64),(256,512),(512,256),(512,512)):
    samples=[((i*997+32768)&65535)-32768 for i in range(incoming)]
    payload=struct.pack('<'+'h'*incoming,*samples)
    cpu.ram[source:source+len(payload)]=payload
    cpu.ram[dest:dest+2048]=b'\xcd'*2048
    cpu.put(nin,incoming);cpu.put(nout,room)
    cpu.put(obj+0x2c,7);cpu.put(obj+0x30,11)
    cpu.put(m.STACK+16,nout)
    cpu.call(0xa27c,[obj,source,nin,dest])
    frames=min(incoming,room)
    assert cpu.ram[dest:dest+frames*2]==payload[:frames*2]
    assert cpu.get(dest+frames*2,2)==0xcdcd
    assert cpu.get(nin)==cpu.get(nout)==frames
    assert cpu.get(obj+0x2c)==7+frames and cpu.get(obj+0x30)==11+frames
    cases.append(dict(input_samples=incoming,room_samples=room,copied_samples=frames))
# Original RCV_ExecSvr returns source descriptors after copying into its
# separate output buffer. Our host sink deliberately retains them longer,
# until consumption, so destruction must release that extra ownership.
cpu=m.Iop(1,16)
vt,inp,out,rcv,tmp=0x29000,m.OBJ,0x23000,0x24000,m.OUT
for off,fn in ((0x18,0x7fbc),(0x1c,0x8310),(0x20,0x8164),(0x24,0x7ea0)):
    cpu.put(vt+off,fn)
cpu.put(inp,vt);cpu.put(out,vt);cpu.put(out+4,0x101)
cpu.put(out+12,0x23100);cpu.put(out+16,16);cpu.call(0x7d80,[out])
for q,line,addr,size in ((inp,1,0x30000,8192),(out,0,0x35000,512)):
    cpu.put(tmp,addr);cpu.put(tmp+4,size);cpu.call(0x8164,[q,line,tmp])
for off,val in ((8,inp),(12,out),(0x24,256),(0x28,32),(0x3c,1),(0x40,0xa27c)):
    cpu.put(rcv+off,val)
cpu.instruction_limit=15000;cpu.call(0x9ff4,[rcv])
returned=cpu.call(0x7ea0,[inp,0]);queued=cpu.call(0x7ea0,[inp,1])
produced=cpu.call(0x7ea0,[out,1])
assert (returned,queued,produced)==(512,7680,512)
# Retail 423d50 restarts only when both channel queues have all 0x4000
# bytes back. Live session's three room descriptors sum to 0x3000;
# losing eight pending 256-frame mono chunks accounts for the exact gap.
room=sum((0x1600,0x800,0x1200));held=8*256*2
assert room==0x3000 and room!=0x4000 and room+held==0x4000
(m.GAME/'logs/cri-pcm-probe.json').write_text(json.dumps(dict(
    irx_sha256=m.SHA,passed=True,cases=cases,
    ownership=dict(original_returned_bytes=returned,original_queued_bytes=queued,
        original_output_bytes=produced,stalled_room_bytes=room,
        host_held_bytes=held,released_room_bytes=room+held),
    scope='Original IOP 0xa27c/0x9ff4 including SJ queue calls; ownership counterexample from opening capture, no live guest writes'),indent=2)+'\n')
print('PASS: 6 original PCM conversions, original source-buffer return, opening restart ownership counterexample')
