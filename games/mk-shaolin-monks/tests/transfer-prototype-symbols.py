"""Transfer the manually verified SelectMenu symbols; never import map addresses directly."""
import csv, hashlib, json, pathlib, re, struct
GAME=pathlib.Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
HASHES={'prototype':'f0f0234bec88bd0d68912b92d86d75d7e6dc14a877bbb7b0c69843517a653f64','retail':'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'}
# Reviewed full disassemblies and map's SelectMenu.obj call/global dependencies.
DEBUG_FUNCTIONS=[('SelectFreeCam',0x195048,0x1aa558,0x60),('SelectFreezeCam',0x1950a8,0x1aa5b8,0x74),('GodModeToggle',0x195480,0x1aaad8,0x68),('SetGodModeText',0x1957b8,0x1aae10,0x6c),('DebugMenuSetup',0x195828,0x1aae80,0x124),('DebugMenuUpdate',0x194030,0x1a88d8,0x188)]
GLOBALS=[('gGodMode',0x4a3568,0x4c7778),('G_Free_Camera',0x4e83d8,0x511378),('gObjectNum',0x4e31e4,0x50c134),('gMenuSystem',0x5aeef0,0x5d1de0),('SelectWindowId',0x5af6f0,0x5d2724),('ActiveWindowId',0x5af6f4,0x5d2728),('DebugMenuWin',0x4a3258,0x4c73a0),('gTest_no_particle_update',0x72fc40,0x6bee10)]
# Memory and file-I/O names are higher priority for reversing current blockers.
FUNCTIONS=[(*row,8) for row in DEBUG_FUNCTIONS]+[
 ('Memory_Initialise',0x1f3508,0x20ecd0,0x158,0x1a8),
 ('Memory_ResetBlock',0x1f3660,0x20ee28,0x230,0x1a8),
 ('Memory_AllocBlock',0x1f3890,0x20f058,0x2d8,0x1a8),
 ('Memory_StatsString',0x1f3d38,0x20f518,0x234,0x1a8),
 ('Memory_InitMallocBlockInfo',0x1f5268,0x210a48,0xa8,0x1a8),
 ('Memory_SetBlock',0x1f5348,0x210b28,0x238,0x1a8),
 ('Memory_Available',0x1f55c8,0x210da8,0x78,0x1a8),
 ('Memory_Used',0x1f5640,0x210e20,0xa8,0x1a8),
 ('StartAsyncHDLoad',0x1f6400,0x212240,0x84,0x1a8),
 ('AdjustLoadInfo',0x1f64b8,0x212380,0x20,0x1a8),
 ('StartAsyncLoadFile',0x1f64d8,0x2123a0,0x1c,0x1a8),
 ('AsyncLoadStatus',0x1f6500,0x2123c8,0xe8,0x1a8),
 ('AsyncProcessLoad',0x1f62d0,0x212110,0xe8,0x1a8),
 ('cPxFileInterface_StartFileLoading',0x34a138,0x370a70,0x28,0x1a8),
 ('cPxFileInterface_FileLoadStatus',0x34a160,0x370a98,0x28,0x1a8),
 ('cPxFileInterface_CloseFileHandle',0x34a1b0,0x370ae8,0x28,0x1a8)]
GLOBALS += [('alloc_blocks',0x6283d8,0x64e998),('alloc_block_f',0x62b7d8,0x651d98),
 ('gMemoryErrorIgnore_f',0x4c7980,0x4ef430),('async_load_info',0x4c7d28,0x4ef7e0),
 ('file_info',0x62b870,0x651e30),('gPxFileInterface',0x7c4408,0x7536d8),('async_info',0x7c67e8,0x755ab8)]
def image(path,expected):
 d=path.read_bytes();assert hashlib.sha256(d).hexdigest()==expected,'Unreviewed ELF'
 h=struct.unpack_from('<16sHHIIIIIHHHHHH',d);heads=[struct.unpack_from('<IIIIIIII',d,h[5]+i*h[9]) for i in range(h[10])]
 def read(a,n):
  for x in heads:
   if x[0]==1 and x[2]<=a and a+n<=x[2]+x[4]:return d[x[1]+a-x[2]:x[1]+a-x[2]+n]
  raise ValueError(hex(a))
 return read

def main():
 p=image(ROOT/'MKSMprototype/SLUS_210.87',HASHES['prototype']);r=image(ROOT/'MortalKombatShaolinMonks/SLUS_210.87',HASHES['retail'])
 rows=[]
 map_text=(ROOT/'MKSMprototype/mka.map').read_text(errors='replace')
 for name,ma,ra,size,delta in FUNCTIONS:
  match=re.search(r'^'+f'{ma:08x} {size:08x}'+r'\s+0\s+(.+)$',map_text,re.M)
  if not match:raise ValueError('Map boundary mismatch: '+name)
  original_name=match[1].strip()
  if original_name.split('(')[0].replace('::','_')!=name:raise ValueError('Map name mismatch: '+name)
  pa=ma-delta;pb=p(pa,size);rb=r(ra,size)
  differences=[]
  for offset in range(0,size,4):
   a,b=struct.unpack_from('<I',pb,offset)[0],struct.unpack_from('<I',rb,offset)[0]
   if a!=b:
    # Reviewed differences are relocated calls/globals, player stride and text IDs.
    assert a>>26==b>>26
    assert (a&0xfc000000)==(b&0xfc000000) if a>>26 in (2,3) else (a&0xffff0000)==(b&0xffff0000)
    differences.append({'offset':offset,'prototype':hex(a),'retail':hex(b)})
  rows.append({'name':name,'original_name':original_name,'kind':'function','map_address':ma,'prototype':pa,'retail':ra,'size':size,'prototype_body_sha256':hashlib.sha256(pb).hexdigest(),'retail_body_sha256':hashlib.sha256(rb).hexdigest(),'reviewed_differences':differences})
 # These two changed substantially. Identity comes from map cross-references,
 # state-machine cases, shared globals and the validated surrounding call graph.
 for name,ma,pa,ra,ps,rs,note in [
  ('Async_DataLoad',0x3518f8,0x351750,0x378a30,0x484,0x400,'Same five-state loader and call graph. Retail adds compressed-size planning and 52-byte requests; prototype uses 48-byte requests and gzip inspection.'),
  ('GetAsyncFileSize',0x1f5ec8,0x1f5d20,0x211878,0x14c,0x2c0,'Retail adds packed 64-bit WAD metadata, file-ID base 30 and an output-size pointer; prototype uses plain offset/size pairs and file-ID base 20.')]:
  rows.append({'name':name,'kind':'function','map_address':ma,'prototype':pa,'retail':ra,'size':rs,'prototype_size':ps,'prototype_body_sha256':hashlib.sha256(p(pa,ps)).hexdigest(),'retail_body_sha256':hashlib.sha256(r(ra,rs)).hexdigest(),'validation':'Semantic call-graph and complete decompilation review; not instruction-equivalent','differences':note})
 for name,pa,ra in GLOBALS:rows.append({'name':name,'kind':'global','prototype':pa,'retail':ra})
 report={'schema_version':1,'elf_sha256':HASHES,'scope':'Reviewed debug-menu, memory-manager and file-I/O subsets; map offsets are NOT globally applicable','validation':'Full paired disassembly, local map boundary alignment, map call/global cross-references. Duplicate async bodies resolved using anchored neighboring HD routine, source order and common callees. See ghidra/*-debug-features.txt.','important_differences':{'player_stride':{'prototype':0x3380,'retail':0x33c0},'god_mode_text_ids':{'prototype':[0x50b,0x50c],'retail':[0x666,0x667]}},'excluded':['SelectDebugMenu: two short-pattern hits are insufficient; 0x1aa59c is inside SelectFreeCam.','All other unreviewed map names.'],'symbols':rows}
 (GAME/'ghidra/retail-symbols.json').write_text(json.dumps(report,indent=2)+'\n')
 with (GAME/'ghidra/verified-symbols.tsv').open('w',newline='') as f:
  out=csv.writer(f,delimiter='\t');out.writerow(['name','kind','prototype','retail','size'])
  for row in rows:out.writerow([row['name'],row['kind'],hex(row['prototype']),hex(row['retail']),row.get('size',0)])
 print(f'PASS: {len(FUNCTIONS)+2} reviewed functions, {len(GLOBALS)} globals; exact retail/prototype identities verified')
if __name__=='__main__':main()
