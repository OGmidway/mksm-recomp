"""Validate the captured bad DIRECT against hash-checked retail VU microcode."""
import json, pathlib, runpy, struct
GAME=pathlib.Path(__file__).resolve().parents[1]
S=runpy.run_path(str(GAME/'tests/transfer-prototype-symbols.py'))
read=S['image'](S['ROOT']/'MortalKombatShaolinMonks/SLUS_210.87',S['HASHES']['retail'])
captured=(GAME/'logs/inspector/vif.bin').read_bytes()
program=read(0x49b8b0,2048)
assert captured[88:88+2048]==program
assert struct.unpack_from('<I',captured,304)[0]==0x5000300d
assert read(0x49b8b0+304-88,4)==struct.pack('<I',0x5000300d)
# The trace's REF header contains NOP, MPG(NUM=0 -> 256 instructions).
# Verify that preserving it loads all bytes, including the would-be DIRECT.
stream=struct.pack('<II',0,0x4a000000)+program
pos=0;loaded=bytearray(16384);commands=[]
while pos<len(stream):
    word,=struct.unpack_from('<I',stream,pos);pos+=4;commands.append(hex(word))
    opcode=(word>>24)&127
    if opcode==0:continue
    assert opcode==0x4a
    size=(((word>>16)&255) or 256)*8;dest=(word&65535)*8
    assert pos+size<=len(stream)
    loaded[dest:dest+size]=stream[pos:pos+size];pos+=size
assert loaded[:2048]==program and commands==['0x0','0x4a000000']
# Exact retail sceDmaSend AND/ORI instructions preserve channel flags.
assert int.from_bytes(read(0x384558,4),'little')==0x2403fff3
assert int.from_bytes(read(0x384568,4),'little')==0x34420105
flags=[dict(before=hex(x),after=hex((x&~12)|0x105)) for x in (0,0x40,0x80,0xc0)]
(GAME/'logs/vif-dma-probe.json').write_text(json.dumps(dict(
    elf_sha256=S['HASHES']['retail'],matched_microcode_bytes=2048,
    microcode_address='0x49b8b0',capture_offset=88,false_direct_offset=304,
    corrected_commands=commands,sdk_channel_flags=flags,passed=True),indent=2)+'\n')
print('PASS: malformed DIRECT is retail VU instruction; restored REF MPG header loads all 2048 bytes')
