"""Validate the retail intro's existing opt-out branch before exposing it for debugging."""
import importlib.util, pathlib, struct, json
GAME=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('symbols',GAME/'tests/transfer-prototype-symbols.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
read=s.image(s.ROOT/'MortalKombatShaolinMonks/SLUS_210.87',s.HASHES['retail'])
word=lambda a:struct.unpack('<I',read(a,4))[0]
assert word(0x1d32e4)==0x3c030051 # lui v1,0x51
assert word(0x1d32ec)==0x8c623a04 # lw v0,0x3a04(v1)
assert word(0x1d32f0)==0x24110001 # s1=1
branch=word(0x1d32fc)
assert branch>>26==4 and branch>>21&31==2 and branch>>16&31==17
target=0x1d3300+(branch&0xffff)*4
assert target==0x1d3410 and word(target)==0x24020003 # state-machine return 3
assert word(0x1d3424)==0x03e00008 # jr ra
assert word(0x513a04)==0 # normal retail behavior remains the default
report={'retail_sha256':s.HASHES['retail'],'entry':'0x1d32e0','flag':'0x513a04',
        'skip_value':1,'branch_target':hex(target),'state_result':3,'default':0,
        'scope':'Existing intro-state opt-out branch; does not fix movie decoding or prove playability','passed':True}
(GAME/'logs/intro-gate-probe.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: exact retail flag load, equality branch and normal state-machine return')
