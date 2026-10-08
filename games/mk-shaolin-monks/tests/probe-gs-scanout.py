"""Inspect live scanout dimensions and create small independent scanline fixtures."""
import hashlib,json,pathlib
GAME=pathlib.Path(__file__).resolve().parents[1]
run=json.loads((GAME/'logs/inspector/report.json').read_text())
capture=json.loads((GAME/'logs/inspector/inspector.json').read_text())
assert run['session']==capture['session']
g=capture['graphics'];display=g['display2'];height=((display>>44)&2047)+1
half=(g['smode2']&3)==3
rows=[y//2 if half else y for y in range(height)]
assert half and height==448 and max(rows)==223
cases=[]
for source_height in (32,224,225):
    source=list(range(source_height))
    output=[v for v in source for _ in range(2)]
    assert len(output)==source_height*2
    cases.append(dict(source_rows=source_height,display_rows=len(output),last_source_row=output[-1]))
result=dict(session=run['session'],smode2=g['smode2'],display2=hex(display),
            display_height=height,source_height=max(rows)+1,
            reference='https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSState.cpp (GSPCRTCRegs::SetRects)',
            frame_sha256=hashlib.sha256((GAME/'logs/inspector/frame.png').read_bytes()).hexdigest(),cases=cases,passed=True)
(GAME/'logs/gs-scanout-probe.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: captured INT+FFMD display is 448 lines from 224 framebuffer rows')
