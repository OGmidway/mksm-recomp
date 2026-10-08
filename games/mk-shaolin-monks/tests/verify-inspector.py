"""Integration checks against the compiled C++ snapshot writer using synthetic RAM."""
import json,os,pathlib,subprocess
GAME=pathlib.Path(__file__).resolve().parent.parent
ROOT=GAME.parents[1]
def main():
    output=GAME/'logs/inspector-fixture.json'
    token='fixture-"quote\\slash\nnewline'
    env=os.environ.copy();env.update(PS2_INSPECTOR_FILE=str(output),PS2_INSPECTOR_SESSION=token,
        PS2_INSPECTOR_WATCHES='last_word=0x1fffffc:4;overflow=0x1ffffff:4;null_pointer=*0x0:4;bad=0x2000000:4;quote"name=0x0:4')
    proc=subprocess.run([str(ROOT/'out/mk-runtime/ps2xRuntime/Release/ps2_irq_stack_check.exe')],env=env,capture_output=True,text=True,timeout=10)
    assert proc.returncode==0,proc.stdout+proc.stderr
    row=json.loads(output.read_text())
    assert row['session']==token and row['state']=='stopped'
    assert isinstance(row['ffmpeg_compiled'],bool)
    assert all(isinstance(row['vu'][key],int) for key in ('vpu_stat','arithmetic_status','vu1_pc','vu1_cycles','vu1_top','vu1_itop'))
    assert all(isinstance(row['vu'][key],bool) for key in ('vu1_ebit','vu1_halt_after_delay','vu1_stopped_d','vu1_stopped_t'))
    assert isinstance(row['vu']['vu0_mac_flags'],int)
    assert len(row['vu']['vu0_vf0_bits'])==4 and all(isinstance(x,int) and 0<=x<=0xffffffff for x in row['vu']['vu0_vf0_bits'])
    assert isinstance(row['mpeg']['initialized'],bool)
    assert all(isinstance(v,int) and v>=0 for k,v in row['mpeg'].items() if k!='initialized')
    watches={w['name']:w for w in row['watches']}
    assert watches['last_word']['valid'] and len(watches['last_word']['bytes'])==8
    assert not watches['overflow']['valid'] and not watches['null_pointer']['valid']
    assert 'bad' not in watches and 'quote"name' in watches
    assert len(row['cpu_history'])<=60 and len(row['rpc'])<=256
    assert len(row['graphics']['contexts'])==2 and len(row['graphics']['recent_events'])<=24
    assert isinstance(row['graphics']['has_host_frame'],bool)
    assert all(isinstance(row['graphics'][key],int) for key in ('pmode','smode2','dispfb1','display1','dispfb2','display2','dma_starts','gif_copies'))
    assert [p['port'] for p in row['pads']]==[0,1]
    assert all(isinstance(p['reads'],int) and isinstance(p['open'],bool) for p in row['pads'])
    assert row['written_unix_ms']>=row['captured_unix_ms']
    print('PASS: compiled writer JSON escaping, RAM bounds, pointer validation, stopped snapshot, graphics/pad fields and history limits')
if __name__=='__main__':main()
