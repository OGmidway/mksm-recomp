"""Read and test fresh snapshots from the PS2Recomp Runtime Inspector."""
import argparse, collections, hashlib, json, os, pathlib, subprocess, time, uuid, struct
ROOT = pathlib.Path(__file__).resolve().parents[3]
GAME = pathlib.Path(__file__).resolve().parents[1]
RUN = GAME / 'logs/inspector'
WATCHES = 'system_heap=0x64e998:104;archive_request=*0x755ac8:52;libc_break=0x563fd4:4;gGodMode=0x4c7778:4;G_Free_Camera=0x511378:4;gObjectNum=0x50c134:4;menu_exit_requested=0x5d1e4c:4;gTest_no_particle_update=0x6bee10:4;async_load_info=0x4ef7e0:16;file_info=0x651e30:8'
FRONTEND_WATCHES = 'frontend_dialog=0x5ca0c8:64;frontend_choices=*0x5ca0d4:76;frontend_character=0x5cc5f0:176;frontend_mode=0x5ca2ac:4;frontend_profile=*0x4d8b80:216;sound_manager=0x678678:32'

def validate(row, session=None, pid=None, now=None):
    if row.get('schema_version') != 1: raise ValueError('Unsupported inspector schema')
    if session is not None and row.get('session') != session: raise ValueError('Wrong or stale runner session')
    if pid is not None and row.get('pid') != pid: raise ValueError('Wrong runner PID')
    captured = row['captured_unix_ms']
    now = time.time()*1000 if now is None else now
    if not 0 <= now-captured <= 3000: raise ValueError('Stale or future CPU snapshot')
    if row['sequence'] < 1: raise ValueError('No CPU sample captured')
    for name in ('cpu','threads','rpc','heap','watches'):
        if name not in row: raise ValueError('Missing '+name)
    return row

def symbol_at(address):
    for item in SYMBOLS:
        if item['kind']=='function' and item['retail']<=address<item['retail']+item['size']:
            return item['name']+f"+0x{address-item['retail']:x}"
    return None

SYMBOLS = json.loads((GAME/'ghidra/retail-symbols.json').read_text())['symbols']

def decode_game_state(watches):
    data={w['name']:bytes.fromhex(w['bytes']) for w in watches if w['valid']}
    result={}
    if len(data.get('system_heap',b''))==104:
        blob=data['system_heap'];words=struct.unpack('<26I',blob)
        fields=['base','length','low','high','alignment','allow_system_shrink','reset_flags','malloc_mode','free_list','used_list','node_pool','malloc_start']
        block={name:words[i] for i,name in enumerate(fields)}
        block['name']=blob[72:104].split(b'\0',1)[0].decode('ascii',errors='replace')
        # PS2 KSEG aliases are legal pointers. Length is a byte count, not a pointer.
        problems=[]
        if block['length']>0x2000000:problems.append('length exceeds EE RAM')
        if words[0]!=0xffffffff:
            for key in ('base','low','high'):
                if (block[key]&0x1fffffff)>0x2000000:problems.append(key+' outside EE RAM')
            if (words[2]&0x1fffffff)>(words[3]&0x1fffffff):problems.append('low exceeds high')
        block['problems']=problems;result['alloc_blocks_0']=block
    if len(data.get('async_load_info',b''))==16:
        state,unknown4,unknown8,buffer=struct.unpack('<4I',data['async_load_info'])
        result['async_load_info']={'state':state,'state_name':{0:'idle',1:'queued',2:'waiting_for_file_interface'}.get(state,'unknown'),'buffer':buffer,'unknown_4':unknown4,'unknown_8':unknown8}
    if len(data.get('file_info',b''))==8:
        offset,size=struct.unpack('<2I',data['file_info']);result['file_info']={'offset':offset,'size':size}
    # Addresses/fields recovered from retail 13bcc0 -> 131240 -> 1306d8.
    # Report evidence rather than inferring movie progress from an input pulse.
    fields = {
        'frontend_dialog': (64, {'callback':4, 'animation':8, 'choices':12, 'transition':24, 'kind':60}),
        'frontend_choices': (76, {'capacity':4, 'count':8, 'selection':12, 'heading_id':24, 'enabled':40}),
        'frontend_character': (176, {'transition':24, 'selected_character':60, 'confirmed':68}),
        'frontend_profile': (216, {'character_1':204, 'character_2':208, 'difficulty':212}),
        'frontend_mode': (4, {'value':0}),
        'sound_manager': (32, {'active_entries':24}),
    }
    frontend={}
    for name,(size,mapping) in fields.items():
        blob=data.get(name,b'')
        if len(blob)==size: frontend[name.removeprefix('frontend_')]={key:struct.unpack_from('<I',blob,offset)[0] for key,offset in mapping.items()}
    if frontend: result['frontend']=frontend
    return result

def compact(row):
    unknown = collections.Counter((e['sid'],e['rpc']) for e in row['rpc'] if e['flags'] & 64)
    return {'sequence':row['sequence'], 'pid':row['pid'], 'state':row['state'],
            'ffmpeg_compiled':row.get('ffmpeg_compiled'),
            'mpeg':row.get('mpeg'), 'vu':row.get('vu'),
            'pc':hex(row['cpu']['pc']), 'pc_symbol':symbol_at(row['cpu']['pc']), 'ra':hex(row['cpu']['ra']), 'ra_symbol':symbol_at(row['cpu']['ra']), 'threads':len(row['threads']),
            'heap':{k:hex(v) for k,v in row['heap'].items()},
            'rpc_history_overwritten':row.get('rpc_history_overwritten',0),
            'unhandled_in_retained_history':[{'sid':hex(sid),'rpc':hex(rpc),'events':n} for (sid,rpc),n in sorted(unknown.items())],
            'watches':row['watches'], 'game_state':decode_game_state(row['watches']),
            'graphics':row.get('graphics'), 'pads':row.get('pads'),
            'retail_debug_state':{w['name']:int.from_bytes(bytes.fromhex(w['bytes']),'little') for w in row['watches'] if w['valid'] and w['name'] in ('gGodMode','G_Free_Camera','gObjectNum','menu_exit_requested','gTest_no_particle_update')}}

def self_test():
    sample={'schema_version':1,'session':'test','pid':123,'sequence':1,'captured_unix_ms':1000,
            'state':'running','cpu':{'pc':0x379220,'ra':0x379220},'threads':[], 'heap':{}, 'watches':[],
            'rpc':[{'seq':1,'sid':0x534e4446,'rpc':0x1300,'flags':64}], 'rpc_history_overwritten':5}
    validate(sample,'test',123,1500)
    assert compact(sample)['unhandled_in_retained_history'][0]['rpc']=='0x1300'
    assert compact(sample)['rpc_history_overwritten']==5
    for kwargs in ({'session':'other','now':1500},{'pid':999,'now':1500},{'now':5000},{'now':999}):
        try: validate(sample,**kwargs)
        except ValueError: pass
        else: raise AssertionError('Accepted invalid sample '+str(kwargs))
    assert symbol_at(0x20f05c)=='Memory_AllocBlock+0x4'
    assert symbol_at(0x1aa59c)=='SelectFreeCam+0x44'
    blob=bytearray(104);struct.pack_into('<5I',blob,0,0x80100000,0x10000,0x80101000,0x80110000,128)
    watch={'name':'system_heap','valid':True,'bytes':blob.hex()}
    assert not decode_game_state([watch])['alloc_blocks_0']['problems']
    struct.pack_into('<I',blob,4,0x80f00e80);watch['bytes']=blob.hex()
    assert 'length exceeds EE RAM' in decode_game_state([watch])['alloc_blocks_0']['problems']
    dialog=bytearray(64);choices=bytearray(76);profile=bytearray(216)
    struct.pack_into('<I',dialog,12,0x123400);struct.pack_into('<I',choices,8,2)
    struct.pack_into('<I',choices,24,0x5a7);struct.pack_into('<I',profile,212,1)
    frontend_watches=[{'name':name,'valid':True,'bytes':blob.hex()} for name,blob in
                     (('frontend_dialog',dialog),('frontend_choices',choices),('frontend_profile',profile))]
    decoded=decode_game_state(frontend_watches)['frontend']
    assert decoded['dialog']['choices']==0x123400 and decoded['dialog']['kind']==0
    assert decoded['choices']['count']==2 and decoded['choices']['heading_id']==0x5a7
    assert decoded['profile']['difficulty']==1
    frontend_watches[1]['valid']=False
    assert 'choices' not in decode_game_state(frontend_watches)['frontend']
    frontend_watches[2]['bytes']='00'
    assert 'profile' not in decode_game_state(frontend_watches)['frontend']
    assert len((WATCHES+';'+FRONTEND_WATCHES).split(';'))==16
    print('PASS: symbols, allocator bounds, inspector freshness/RPCs and recovered difficulty fields with invalid/truncated watches')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds',type=int,default=20)
    parser.add_argument('--read',type=pathlib.Path,nargs='?',const=RUN/'inspector.json')
    parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--boot-trace',action='store_true',help='Enable bounded retail startup/allocator diagnostics')
    parser.add_argument('--skip-intro',action='store_true',help='Opt into the existing retail intro-state skip branch for frontend debugging; does not fix movies')
    parser.add_argument('--press-cross-at',type=float,action='append',default=[],help='Hold host pad Cross for 0.75 seconds at each requested elapsed time; neutral between pulses; repeatable')
    parser.add_argument('--press-start-at',type=float,action='append',default=[],help='Hold host pad Start for 0.75 seconds; repeatable, with neutral input between pulses')
    parser.add_argument('--left-stick-at',action='append',default=[],metavar='START:DURATION:LX:LY',help='Bounded host left-stick pulse; axes 0..255, neutral 128; repeatable')
    parser.add_argument('--audible',action='store_true',help='Opt into audible output; diagnostic tests are muted by default')
    parser.add_argument('--capture-pcm',action='store_true',help='Overwrite at most 2 MiB of submitted PCM for offline analysis')
    parser.add_argument('--capture-feedback',action='store_true',help='Capture one bounded sprite feedback pass and two 4 MiB VRAM snapshots for Python replay')
    parser.add_argument('--capture-pixels',action='store_true',help='Capture at most 8192 fragment outcomes at two fixed scene pixels')
    parser.add_argument('--pixels-after-feedback',action='store_true',help='Begin pixel provenance at the first captured feedback pass; requires --capture-feedback and --capture-pixels')
    parser.add_argument('--pixel-delay-ms',type=int,default=120000,help='Start pixel provenance after 0..300000 ms')
    parser.add_argument('--capture-primitives',action='store_true',help='Capture at most 4096 raw draws with effective GS state for Python analysis')
    parser.add_argument('--primitive-delay-ms',type=int,default=10000,help='Start primitive capture after 0..300000 ms')
    parser.add_argument('--triangles-only',action='store_true',help='Exclude sprites/lines/points from bounded primitive capture')
    parser.add_argument('--capture-vu1',action='store_true',help='Capture at most eight VU1 execute/resume inputs')
    parser.add_argument('--vu1-distinct-entries',action='store_true',help='Capture only the first start at each distinct VU1 PC, at most eight')
    parser.add_argument('--vu1-on-xgkick-failure',action='store_true',help='Retain the last eight input calls preceding the first XGKICK failure')
    parser.add_argument('--vu1-entry-pc',type=lambda v:int(v,0),help='Capture only starts at this aligned VU1 byte PC')
    parser.add_argument('--vu1-delay-ms',type=int,default=0,help='Start VU1 capture after 0..300000 ms from first execute')
    parser.add_argument('--vif-launch-pc',type=lambda v:int(v,0),help='Capture one command stream launching this VU1 PC, at most 1 MiB')
    parser.add_argument('--capture-gif',action='store_true',help='Overwrite the bounded GIF byte capture (512 packets, 8 MiB payload maximum)')
    parser.add_argument('--gif-delay-ms',type=int,default=0,help='Start bounded GIF capture after 0..300000 ms')
    parser.add_argument('--frame-interval-ms',type=int,default=2000,help='Overwrite the same frame capture every 16..10000 ms (default 2000)')
    parser.add_argument('--memory-watch',action='append',default=[],help='Additional read-only name=0xaddress:size or name=*0xpointer:size')
    parser.add_argument('--trace-frontend',action='store_true',help='Watch character confirmation, difficulty dialog/profile and active sounds; retain at most 48 state changes')
    args=parser.parse_args()
    if args.vu1_on_xgkick_failure and (not args.capture_vu1 or args.vu1_entry_pc is not None or args.vu1_distinct_entries or args.vu1_delay_ms): parser.error('failure capture requires --capture-vu1 without entry, delay or distinct filters')
    if args.vif_launch_pc is not None and (not 0<=args.vif_launch_pc<16384 or args.vif_launch_pc%8): parser.error('VIF launch PC must be aligned and below 0x4000')
    if args.self_test: return self_test()
    if args.pixels_after_feedback and not (args.capture_feedback and args.capture_pixels): parser.error('--pixels-after-feedback requires both capture flags')
    if args.read:
        row=json.loads(args.read.read_text());report=compact(row)
        report['age_seconds']=round((time.time()*1000-row['captured_unix_ms'])/1000,2)
        report['fresh']=0<=report['age_seconds']<=3
        print(json.dumps(report,indent=2));return
    if not 1<=args.seconds<=420: parser.error('seconds must be 1..420')
    if not 0<=args.primitive_delay_ms<=300000: parser.error('primitive delay must be 0..300000 ms')
    if args.vu1_entry_pc is not None and (not 0<=args.vu1_entry_pc<16384 or args.vu1_entry_pc%8): parser.error('VU1 entry PC must be aligned and below 0x4000')
    if not 0<=args.vu1_delay_ms<=300000: parser.error('VU1 delay must be 0..300000 ms')
    if not 0<=args.gif_delay_ms<=300000: parser.error('GIF delay must be 0..300000 ms')
    if not 16<=args.frame_interval_ms<=10000: parser.error('frame interval must be 16..10000 ms')
    stick_pulses=[]
    for value in args.left_stick_at:
        try:
            start,duration,lx,ly=value.split(':')
            start,duration=float(start),float(duration)
            lx,ly=int(lx),int(ly)
        except ValueError:
            parser.error('left-stick pulse requires START:DURATION:LX:LY')
        if not (0<=start and 0<duration<=30 and start+duration<=args.seconds-1 and 0<=lx<=255 and 0<=ly<=255):
            parser.error('left-stick pulse must have axes 0..255, duration 0..30 seconds, and finish before timeout')
        if any(start<t+d and t<start+duration for t,d,_,_ in stick_pulses):
            parser.error('left-stick pulses must not overlap')
        stick_pulses.append((start,duration,lx,ly))
    if not 0<=args.pixel_delay_ms<=300000: parser.error('pixel delay must be 0..300000 ms')
    input_times=args.press_cross_at+args.press_start_at
    if any(not 0<=t<=args.seconds-1 for t in input_times):
        parser.error('pad pulses must leave at least one second before timeout')
    RUN.mkdir(parents=True,exist_ok=True)
    exe=ROOT/'out/mk-runtime/ps2xRuntime/Release/ps2EntryRunner.exe'
    elf=ROOT/'MortalKombatShaolinMonks/SLUS_210.87'
    expected='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
    if hashlib.sha256(elf.read_bytes()).hexdigest()!=expected: raise RuntimeError('Unexpected MKSM ELF')
    token=uuid.uuid4().hex
    watches=[WATCHES]+([FRONTEND_WATCHES] if args.trace_frontend else [])+args.memory_watch
    if sum(len(w.split(';')) for w in watches)>16: parser.error('Inspector supports at most 16 watches; --trace-frontend uses all 16')
    env=os.environ.copy();env.update(PS2_INSPECTOR_FILE=str(RUN/'inspector.json'),PS2_INSPECTOR_SESSION=token,PS2_INSPECTOR_WATCHES=';'.join(watches))
    for file in (RUN/'frame.png',RUN/'frame.png.json'): file.unlink(missing_ok=True)
    env.setdefault('PS2_CONTROLLER_CONFIG',str(GAME/'config/controllers.ini'))
    env['PS2_INSPECTOR_FRAME']=str(RUN/'frame.png')
    env['PS2_INSPECTOR_FRAME_INTERVAL_MS']=str(args.frame_interval_ms)
    env['PS2_INSPECTOR_GIF_DELAY_MS']=str(args.gif_delay_ms)
    env['PS2_INSPECTOR_PRIMITIVES_DELAY_MS']=str(args.primitive_delay_ms)
    env['PS2_INSPECTOR_TRIANGLES_ONLY']='1' if args.triangles_only else '0'
    env.pop('PS2X_BOOT_TRACE',None)
    env.pop('MKSM_DEBUG_SKIP_INTRO',None)
    env.pop('PS2_INSPECTOR_PAD_FILE',None)
    env.pop('PS2_INSPECTOR_VU1',None)
    env.pop('PS2_INSPECTOR_XGKICK',None)
    env.pop('PS2_INSPECTOR_VIF_LAUNCH',None)
    env.pop('PS2_INSPECTOR_VIF_LAUNCH_PC',None)
    if args.vif_launch_pc is not None:
        (RUN/'vif-launch.bin').unlink(missing_ok=True)
        env['PS2_INSPECTOR_VIF_LAUNCH']=str(RUN/'vif-launch.bin')
        env['PS2_INSPECTOR_VIF_LAUNCH_PC']=hex(args.vif_launch_pc)
    env['PS2_INSPECTOR_VU1_ON_XGKICK_FAILURE']='1' if args.vu1_on_xgkick_failure else '0'
    env.pop('PS2_INSPECTOR_VU1_ENTRY_PC',None)
    if args.vu1_entry_pc is not None: env['PS2_INSPECTOR_VU1_ENTRY_PC']=hex(args.vu1_entry_pc)
    env['PS2_INSPECTOR_VU1_DELAY_MS']=str(args.vu1_delay_ms)
    env['PS2_INSPECTOR_VU1_DISTINCT_ENTRIES']='1' if args.vu1_distinct_entries else '0'
    env.pop('PS2_INSPECTOR_GIF',None)
    env.pop('PS2_INSPECTOR_PCM',None)
    env.pop('PS2_INSPECTOR_PRIMITIVES',None)
    env.pop('PS2_INSPECTOR_FEEDBACK',None)
    env['PS2_INSPECTOR_FEEDBACK_DELAY_MS']=str(args.pixel_delay_ms)
    if args.capture_feedback:
        for suffix in ('.csv','.start.bin','.end.bin','.stop.json'):(RUN/('feedback'+suffix)).unlink(missing_ok=True)
        env['PS2_INSPECTOR_FEEDBACK']=str(RUN/'feedback')
    env.pop('PS2_INSPECTOR_PIXELS_AFTER_FEEDBACK',None)
    if args.pixels_after_feedback: env['PS2_INSPECTOR_PIXELS_AFTER_FEEDBACK']='1'
    env.pop('PS2_INSPECTOR_PIXELS',None)
    env['PS2_INSPECTOR_PIXELS_DELAY_MS']=str(args.pixel_delay_ms)
    if args.capture_pixels:
        (RUN/'pixels.csv').unlink(missing_ok=True)
        env['PS2_INSPECTOR_PIXELS']=str(RUN/'pixels.csv')
    env['PS2_AUDIO_MUTE']='0' if args.audible else '1'
    env['PS2_MISSING_TARGET_LIMIT']='16'
    if args.capture_primitives:
        (RUN/'primitives.csv').unlink(missing_ok=True)
        env['PS2_INSPECTOR_PRIMITIVES']=str(RUN/'primitives.csv')
    if args.capture_vu1:
        (RUN/'vu1-starts.bin').unlink(missing_ok=True)
        env['PS2_INSPECTOR_VU1']=str(RUN/'vu1-starts.bin')
        (RUN/'xgkick-failure.bin').unlink(missing_ok=True)
        env['PS2_INSPECTOR_XGKICK']=str(RUN/'xgkick-failure.bin')
    if args.capture_pcm:
        (RUN/'pcm.bin').unlink(missing_ok=True)
        env['PS2_INSPECTOR_PCM']=str(RUN/'pcm.bin')
    if args.capture_gif:
        (RUN/'gif.bin').unlink(missing_ok=True)
        env['PS2_INSPECTOR_GIF']=str(RUN/'gif.bin')
    pad_file=RUN/'pad.txt'
    pad_file.unlink(missing_ok=True)
    if input_times or stick_pulses:env['PS2_INSPECTOR_PAD_FILE']=str(pad_file)
    if args.skip_intro: env['MKSM_DEBUG_SKIP_INTRO']='1'
    if args.boot_trace: env['PS2X_BOOT_TRACE']='1'
    startup=None
    if os.name=='nt':
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    seen=[]; latest=None; first_heap_anomaly=None; read_errors=0; unknown=collections.Counter(); last_rpc=0; gaps=0
    with (RUN/'stdout.log').open('w') as out,(RUN/'stderr.log').open('w') as err:
        proc=subprocess.Popen([str(exe),str(elf)],cwd=GAME/'runtime',env=env,stdout=out,stderr=err,startupinfo=startup)
        runner_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()
        deadline=time.monotonic()+args.seconds
        started=time.monotonic()
        stick_samples=[];last_stick_sample=-1.0
        input_observed=False;start_observed=False;input_samples=[];physical_samples=[];previous_physical=None
        mpeg_samples=collections.deque(maxlen=48);previous_mpeg=None
        frontend_samples=collections.deque(maxlen=48);previous_frontend=None
        custom_watch_names={part.split('=',1)[0] for group in args.memory_watch for part in group.split(';')}
        custom_watch_samples=collections.deque(maxlen=48);previous_custom_watches=None
        vf0_samples_checked=0;first_vf0_anomaly=None;first_negative_broadphase_bound=None
        try:
            while time.monotonic()<deadline and proc.poll() is None:
                elapsed=time.monotonic()-started
                if input_times or stick_pulses:
                    buttons=49151 if any(t<=elapsed<t+.75 for t in args.press_cross_at) else 65535
                    if any(t<=elapsed<t+.75 for t in args.press_start_at):buttons &= ~8
                    lx,ly=next(((x,y) for t,d,x,y in stick_pulses if t<=elapsed<t+d),(128,128))
                    temporary=pad_file.with_suffix('.tmp')
                    temporary.write_text(f'{token} {int(time.time()*1000)+1000} {buttons} {lx} {ly} 128 128\n')
                    try:temporary.replace(pad_file)
                    except PermissionError:pass # Reader may briefly hold the file on Windows; lease survives a missed refresh.
                try:
                    row=validate(json.loads((RUN/'inspector.json').read_text()),token,proc.pid)
                    if not seen or row['sequence']!=seen[-1]:
                        if seen and row['sequence']<=seen[-1]: raise RuntimeError('Snapshot sequence reversed')
                        seen.append(row['sequence']);latest=row
                        vf0=row.get('vu',{}).get('vu0_vf0_bits')
                        if vf0 is not None:
                            vf0_samples_checked+=1
                            if vf0!=[0,0,0,1065353216] and first_vf0_anomaly is None:
                                first_vf0_anomaly={'elapsed':round(elapsed,3),'sequence':row['sequence'],'pc':hex(row['cpu']['pc']),'bits':vf0}
                        if row['cpu']['pc']==0x3a0380 and first_negative_broadphase_bound is None:
                            raw_bound=row['cpu']['gpr_u64'][23]
                            if raw_bound>>63:
                                first_negative_broadphase_bound={'elapsed':round(elapsed,3),'sequence':row['sequence'],'bound':raw_bound-(1<<64)}
                        custom_watches=[w for w in row['watches'] if w['name'] in custom_watch_names]
                        if custom_watches and custom_watches!=previous_custom_watches:
                            custom_watch_samples.append({'elapsed':round(elapsed,3),'sequence':row['sequence'],'pc':hex(row['cpu']['pc']),'watches':custom_watches})
                            previous_custom_watches=custom_watches
                        if stick_pulses and elapsed-last_stick_sample>=1 and len(stick_samples)<96 and any(t-3<=elapsed<=t+d+3 for t,d,_,_ in stick_pulses):
                            stick_samples.append({'elapsed':round(elapsed,3),'sequence':row['sequence'],'commanded_left_stick':[lx,ly],'pads':row.get('pads',[]),'watches':custom_watches})
                            last_stick_sample=elapsed
                        frontend=decode_game_state(row['watches']).get('frontend')
                        if args.trace_frontend and frontend!=previous_frontend:
                            frontend_samples.append({'elapsed':round(elapsed,3),'sequence':row['sequence'],'state':frontend})
                            previous_frontend=frontend
                        physical=[p for p in row.get('pads',[]) if p.get('last_read_ok') and not p.get('used_override')]
                        physical_key=[(p['port'],p['buttons_active_low']) for p in physical]
                        if row.get('mpeg')!=previous_mpeg:
                            previous_mpeg=row.get('mpeg')
                            mpeg_samples.append({'elapsed':round(elapsed,3),'mpeg':previous_mpeg})
                        if physical_key!=previous_physical and len(physical_samples)<48:
                            physical_samples.append({'elapsed':round(elapsed,3),'pads':physical})
                        previous_physical=physical_key
                        input_observed |= any(p.get('used_override') and p.get('buttons_active_low')==49151 for p in row.get('pads',[]))
                        start_observed |= any(p.get('used_override') and not (p.get('buttons_active_low',65535)&8) for p in row.get('pads',[]))
                        if any(t-1<=elapsed<=t+2 for t in input_times) and len(input_samples)<48:
                            input_samples.append({'elapsed':round(elapsed,3),'pc':row['cpu']['pc'],'pads':row.get('pads'),
                                                  'watches':[w for w in row['watches'] if w['name'].startswith('input_')]})
                        heap=decode_game_state(row['watches']).get('alloc_blocks_0',{})
                        if first_heap_anomaly is None and heap.get('problems'):
                            first_heap_anomaly={'sequence':row['sequence'],'pc':hex(row['cpu']['pc']),'pc_symbol':symbol_at(row['cpu']['pc']),'heap':heap}
                        for event in row['rpc']:
                            seq=event['seq']
                            if seq>last_rpc:
                                gaps+=max(0,seq-last_rpc-1);last_rpc=seq
                                if event['flags']&64: unknown[(event['sid'],event['rpc'])]+=1
                        if len(seen)%4==0: print(f"sample={row['sequence']} pc={row['cpu']['pc']:#x} threads={len(row['threads'])}",flush=True)
                except (OSError,ValueError,KeyError): read_errors+=1
                time.sleep(.1)
        finally:
            timed_out=proc.poll() is None
            if timed_out:
                proc.terminate()
                try:proc.wait(5)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
            pad_file.unlink(missing_ok=True)
            pad_file.with_suffix('.tmp').unlink(missing_ok=True)
    report={'session':token,'pid':proc.pid,'elf_sha256':expected,'runner_sha256':runner_sha256,
            'press_cross_at':args.press_cross_at,'cross_observed_in_pad_reads':input_observed,
            'press_start_at':args.press_start_at,'start_observed_in_pad_reads':start_observed,
            'input_samples':input_samples,'left_stick_pulses':stick_pulses,'stick_samples':stick_samples,
            'physical_input_samples':physical_samples,
            'mpeg_samples':list(mpeg_samples),
            'frontend_samples':list(frontend_samples),
            'custom_watch_samples':list(custom_watch_samples),
            'vf0_samples_checked':vf0_samples_checked,'first_vf0_anomaly':first_vf0_anomaly,
            'first_negative_broadphase_bound':first_negative_broadphase_bound,
            'debug_skip_intro':args.skip_intro,'audio_muted':not args.audible,
            'first_heap_anomaly':first_heap_anomaly,'fresh_samples_seen':len(seen),'read_retries':read_errors,'rpc_events_missed_between_polls':gaps,
            'unhandled_observed':[{'sid':hex(s),'rpc':hex(r),'events':n} for (s,r),n in sorted(unknown.items())],
            'timed_out':timed_out,'exit_code':proc.returncode,'playability_verified':False,'latest':compact(latest) if latest else None}
    # Read after our runner has stopped; previous frame files were removed before launch.
    frame=RUN/'frame.png'; metadata=RUN/'frame.png.json'
    if frame.exists() and metadata.exists():
        try:
            report['frame_capture']={'path':str(frame),'sha256':hashlib.sha256(frame.read_bytes()).hexdigest(),
                                     'metadata':json.loads(metadata.read_text()),'session':token}
        except (OSError,ValueError) as error:
            report['frame_capture_error']=str(error)
    diagnostic_lines=(RUN/'stderr.log').read_text(errors='replace').splitlines()
    if args.capture_gif and (RUN/'gif.bin').exists():
        report['gif_capture']={'path':str(RUN/'gif.bin'),'session':token,
                               'sha256':hashlib.sha256((RUN/'gif.bin').read_bytes()).hexdigest()}
    if args.capture_feedback:
        report['feedback_capture']={'session':token,'files':{suffix:hashlib.sha256((RUN/('feedback'+suffix)).read_bytes()).hexdigest() for suffix in ('.csv','.start.bin','.end.bin','.stop.json') if (RUN/('feedback'+suffix)).exists()}}
    if args.capture_pixels and (RUN/'pixels.csv').exists():
        report['pixel_capture']={'path':str(RUN/'pixels.csv'),'session':token,'delay_ms':args.pixel_delay_ms,'after_feedback':args.pixels_after_feedback,
                                'sha256':hashlib.sha256((RUN/'pixels.csv').read_bytes()).hexdigest()}
    if args.capture_primitives and (RUN/'primitives.csv').exists():
        report['primitive_capture']={'path':str(RUN/'primitives.csv'),'session':token,'delay_ms':args.primitive_delay_ms,'triangles_only':args.triangles_only,
                               'sha256':hashlib.sha256((RUN/'primitives.csv').read_bytes()).hexdigest()}
    if args.capture_vu1 and (RUN/'vu1-starts.bin').exists():
        report['vu1_capture']={'path':str(RUN/'vu1-starts.bin'),'session':token,'delay_ms':args.vu1_delay_ms,'distinct_entries':args.vu1_distinct_entries,'entry_pc':args.vu1_entry_pc,'on_xgkick_failure':args.vu1_on_xgkick_failure,
                               'sha256':hashlib.sha256((RUN/'vu1-starts.bin').read_bytes()).hexdigest()}
    if args.capture_vu1 and (RUN/'xgkick-failure.bin').exists():
        report['xgkick_failure_capture']={'session':token,'path':str(RUN/'xgkick-failure.bin'),'sha256':hashlib.sha256((RUN/'xgkick-failure.bin').read_bytes()).hexdigest()}
    if args.vif_launch_pc is not None and (RUN/'vif-launch.bin').exists():
        report['vif_launch_capture']={'session':token,'pc':args.vif_launch_pc,'path':str(RUN/'vif-launch.bin'),'sha256':hashlib.sha256((RUN/'vif-launch.bin').read_bytes()).hexdigest()}
    report['xgkick_failures']=[line for line in diagnostic_lines if '[VU XGKICK failure]' in line][:8]
    report['mute_confirmed']=any('[audio] Host output muted;' in line for line in diagnostic_lines)
    if args.capture_pcm and (RUN/'pcm.bin').exists():
        report['pcm_capture']={'path':str(RUN/'pcm.bin'),'session':token,
                               'sha256':hashlib.sha256((RUN/'pcm.bin').read_bytes()).hexdigest()}
    report['last_missing_targets']=[line for line in diagnostic_lines if '[guest-branch:missing-target]' in line][-16:]
    report['missing_target_limit']=16
    report['missing_target_limit_reached']=len(report['last_missing_targets'])>=16
    report['last_runtime_errors']=[line for line in diagnostic_lines if 'Error during program execution:' in line][-2:]
    (RUN/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
    if len(seen)<3: raise RuntimeError('Fewer than three fresh live samples; inspect logs')
if __name__=='__main__': main()
