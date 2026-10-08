"""Check retail CRI finalization instructions and a captured movie stall.

0x44cdd8 calls 0x44c748 to round/pad the last ring block before publishing
codec+0x1144/0x1148. An empty ring without these flags is only starvation.
"""
import argparse, importlib.util, json, pathlib, struct
GAME=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('symbols',GAME/'tests/transfer-prototype-symbols.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
read=s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',s.HASHES['retail'])
word=lambda a: struct.unpack('<I',read(a,4))[0]
assert word(0x44cdf8)==0x24901128 # codec flags base
assert word(0x44ce08)==0x0c1131d2 # jal 0x44c748 (final block padding)
assert word(0x44ce10)==0xae110020 # store caller's end flag
assert word(0x44ce14)==0xae12001c # publish finalization
# Original signed arithmetic for nonnegative producer counts. Even an exact
# boundary gets room for the sequence terminator; do not submit a partial twice.
padding=[{'partial':n,'padded':((n+0x803)>>11)<<11} for n in (0,1,274,2044,2045,2047)]
assert [x['padded'] for x in padding]==[2048,2048,2048,2048,4096,4096]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--capture-stall',action='store_true',help='Replace the baseline from a live snapshot with mpeg_ring watch at codec+0x1128 (224 bytes).')
args=parser.parse_args()
target=GAME/'logs/cri-completion-probe.json'
if args.capture_stall:
 snap=json.loads((GAME/'logs/inspector/inspector.json').read_text())
 w=next(x for x in snap['watches'] if x['name']=='mpeg_ring' and x['valid'])
 data=bytes.fromhex(w['bytes']);u32=lambda off:struct.unpack_from('<I',data,off)[0]
 state={'finalized':u32(0x1c),'end_flag':u32(0x20),'full_blocks':u32(0x80),'pending_bytes':u32(0x84)}
else:
 snap=json.loads(target.read_text());state=snap['producer']
assert state==dict(finalized=1,end_flag=1,full_blocks=0,pending_bytes=0),state
report={'elf_sha256':s.HASHES['retail'],'session':snap['session'],'padding_cases':padding,
        'producer':state,'mpeg':snap['mpeg'],'scope':'Retail instruction check and live producer EOF; not a gameplay proof'}
if args.capture_stall: target.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
