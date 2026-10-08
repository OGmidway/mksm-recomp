"""Validate retail packed-WAD sign handling against actual archive bytes."""
import pathlib,struct,json,zlib
GAME=pathlib.Path(__file__).resolve().parents[1];wad=GAME.parents[1]/'MortalKombatShaolinMonks/GAMEDATA.WAD'
with wad.open('rb') as f:
 header=f.read(0xe800);table=0x800 if struct.unpack_from('<I',header,8)[0]==1 else 0x20
 if struct.unpack_from('<Q',header,table)[0]==0:table=0x800
 cases=[]
 for file_id in [30,34,35]:
  value=struct.unpack_from('<Q',header,table+(file_id-30)*8)[0];packed=bool(value>>63)
  offset=(value<<11)&0xffffffff if packed else value&0xffffffff
  stored=(value>>22)&0x3fffff if packed else value>>32
  unpacked=((value>>44)&0x7ffff)<<3 if packed else stored
  row={'file_id':file_id,'raw':hex(value),'packed':packed,'offset':offset,'stored':stored,'unpacked':unpacked,'old_bgez_taken':not bool(value&0x80000000),'correct_bgez_taken':not packed}
  if file_id==35:
   assert value==0x803aa0026d400028 and (offset,stored,unpacked)==(0x14000,0x9b5,0x1d50)
   assert row['old_bgez_taken'] and not row['correct_bgez_taken']
  if packed:
   f.seek(offset);data=f.read(stored);row['compressed_prefix']=data[:12].hex();decoded=None
   for mode in (47,15,-15):
    try:
     decoder=zlib.decompressobj(mode);out=decoder.decompress(data,unpacked+1)
     if decoder.eof and len(out)<=unpacked:decoded=len(out);break
    except zlib.error:pass
   row['python_inflate_bytes']=decoded
   # Packed sizes are stored in units of eight bytes; trailing alignment is allowed.
   if decoded is not None:assert 0<=unpacked-decoded<8
  cases.append(row)
result={'passed':True,'cases':cases,'instruction':'BGEZ compares signed low 64 bits; bit 31 is insufficient'}
(GAME/'logs/wad-branch-probe.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
