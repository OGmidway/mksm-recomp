"""Compare live movie buffer state with the original retail display instructions."""
from pathlib import Path
import importlib.util,json,struct
GAME=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('display',GAME/'tests/probe-gs-display.py')
display=importlib.util.module_from_spec(spec);spec.loader.exec_module(display)
run=json.loads((GAME/'logs/inspector/report.json').read_text())
snap=json.loads((GAME/'logs/inspector/inspector.json').read_text())
assert run['session']==snap['session'] and snap['mpeg']['pictures_served']>0
w={x['name']:bytes.fromhex(x['bytes']) for x in snap['watches'] if x['valid']}
params=struct.unpack_from('<4H',w['gs_params'])
width,height=struct.unpack('<2I',w['movie_dimensions'])
assert params[:3]==(1,2,0) and (width,height)==(512,448), (params,width,height)
read=display.s.image(display.s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',display.s.HASHES['retail'])
expected=display.execute(read,params,(0,width,height,0,0))
assert expected==display.expected(params,(0,width,height,0,0))
actual=list(struct.unpack_from('<5Q',w['movie_buffers']))
assert expected[1]==1 and ((expected[3]>>44)&2047)+1==448
report=dict(session=run['session'],params=params,dimensions=[width,height],
            actual=actual,expected=expected,matches=actual==expected,
            expected_source_rows=448,actual_source_rows=224 if actual[1]&3==3 else 448,
            scope='Exact retail display routine executed in Python; first movie display descriptor compared with live RAM')
(GAME/'logs/movie-display-probe.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
