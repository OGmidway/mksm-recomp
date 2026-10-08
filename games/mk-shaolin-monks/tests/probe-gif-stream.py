"""Read bounded PS2_INSPECTOR_GIF records; inspect packet boundaries without guest writes."""
import argparse
import hashlib
import json
import pathlib
import struct

GAME = pathlib.Path(__file__).resolve().parents[1]


def inspect(data):
    offset = 0
    tags = []
    writes = []
    while offset + 16 <= len(data):
        start = offset
        lo, hi = struct.unpack_from('<QQ', data, offset)
        loops, fmt, nreg = lo & 32767, (lo >> 58) & 3, (lo >> 60) or 16
        registers = [(hi >> (4*i)) & 15 for i in range(nreg)]
        payload = loops * nreg * 16 if fmt == 0 else ((loops*nreg+1)//2)*16 if fmt == 1 else loops*16
        offset += 16
        tag = dict(offset=start, lo=f'{lo:016x}', hi=f'{hi:016x}', loops=loops,
                   format=fmt, registers=registers, payload=payload, available=len(data)-offset,
                   eop=bool(lo & 32768), reserved_nonzero=bool(lo & 0x3fffffff0000))
        tags.append(tag)
        if payload > len(data)-offset:
            tag['truncated'] = True
            break
        if fmt == 0:
            for i in range(loops*nreg):
                if registers[i % nreg] == 14:
                    value, reg = struct.unpack_from('<QQ', data, offset+i*16)
                    if len(writes) < 32:
                        writes.append(dict(offset=offset+i*16, register=hex(reg), value=f'{value:016x}'))
        offset += payload
    return dict(bytes=len(data), tags=tags, writes=writes, end=offset)



def classify_boundaries(packets):
    split_images=[];anomalies=[]
    for i,packet in enumerate(packets):
        for tag in packet['tags']:
            split=False
            if (tag.get('truncated') and tag['format']==2 and tag['available']==0
                    and tag['offset']==0 and packet['bytes']==16 and not tag['reserved_nonzero']
                    and i+1<len(packets)):
                following=packets[i+1]
                if len(following['tags'])==1:
                    other=following['tags'][0]
                    split=(other['format']==2 and other['loops']==tag['loops']
                           and other['available']==other['payload']
                           and not other.get('truncated') and not other['reserved_nonzero'])
                if split:split_images.append(dict(header_packet=i,payload_packet=i+1,bytes=tag['payload']))
            if (tag.get('truncated') or tag['reserved_nonzero']) and not split:
                anomalies.append(dict(packet=i,**tag))
    return split_images,anomalies


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/inspector')
    run_dir=parser.parse_args().run_dir
    raw=(run_dir/'gif.bin').read_bytes()
    assert len(raw) <= 8*1024*1024 + 512*4, 'Capture exceeds runtime limits'
    run=json.loads((run_dir/'report.json').read_text())
    assert run['gif_capture']['session']==run['session']
    assert run['gif_capture']['sha256']==hashlib.sha256(raw).hexdigest()
    packets=[]; offset=0
    while offset < len(raw):
        size,=struct.unpack_from('<I',raw,offset);offset+=4
        assert 16 <= size <= 8*1024*1024 and offset+size <= len(raw)
        packets.append(inspect(raw[offset:offset+size]));offset+=size
        assert len(packets)<=512, 'Capture exceeds packet limit'
    split_images,anomalies=classify_boundaries(packets)
    report=dict(session=run['session'],runner_sha256=run['runner_sha256'],
                packets=len(packets), bytes=len(raw), capture_empty=not packets,
                interpretation=('Empty capture; no draw-packet validation evidence' if not packets else 'Packet boundaries inspected; rendering correctness not established'),
                split_image_headers=split_images[:32], split_image_header_count=len(split_images),
                anomalies=anomalies[:32],
                anomaly_count=len(anomalies), packet_details=packets)
    (GAME/'logs/gif-stream-probe.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='packet_details'},indent=2))


if __name__=='__main__':main()
