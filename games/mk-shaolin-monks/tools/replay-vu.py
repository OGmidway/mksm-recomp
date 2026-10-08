"""Run a hash-checked captured VU call through the native interpreter, without a game boot."""
import argparse,hashlib,importlib.util,json,os,pathlib,subprocess
GAME=pathlib.Path(__file__).resolve().parents[1];ROOT=GAME.parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--run-dir',type=pathlib.Path,default=GAME/'logs/first-stage-relinked');ap.add_argument('--record',type=int,default=0);ap.add_argument('--cycles',type=int,default=1000000);args=ap.parse_args()
    if not 1<=args.cycles<=1000000:ap.error('cycles must be 1..1000000')
    if not 0<=args.record<8:ap.error('record must be 0..7')
    run=json.loads((args.run_dir/'report.json').read_text());path=args.run_dir/'vu1-starts.bin';raw=path.read_bytes();meta=run['vu1_capture']
    assert hashlib.sha256(raw).hexdigest()==meta['sha256'] and meta['session']==run['session']
    spec=importlib.util.spec_from_file_location('vu_probe',GAME/'tests/probe-vu1-starts.py');probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)
    records=probe.parse(raw);assert args.record<len(records),'record absent from capture'
    exe=ROOT/'out/mk-runtime/ps2xRuntime/Release/ps2_vu_replay.exe'
    if not exe.exists():raise RuntimeError('Build target ps2_vu_replay in out/mk-runtime first')
    env=os.environ.copy()
    for key in ('PS2_INSPECTOR_FILE','PS2_INSPECTOR_VU1','PS2_INSPECTOR_GIF','PS2_INSPECTOR_PRIMITIVES','PS2_INSPECTOR_PCM'):env.pop(key,None)
    failure=args.run_dir/'vu-replay-xgkick.bin';failure.unlink(missing_ok=True);env['PS2_INSPECTOR_XGKICK']=str(failure)
    data=args.run_dir/'vu-replay-data.bin';data.unlink(missing_ok=True)
    completed=subprocess.run([str(exe),str(path),str(args.record),str(data),str(args.cycles)],env=env,capture_output=True,text=True,timeout=20)
    report=dict(source_session=run['session'],source_runner_sha256=run['runner_sha256'],replay_exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),record=args.record,cycle_budget=args.cycles,entry=records[args.record]['pc'],originally_resumed=records[args.record]['resumed'],returncode=completed.returncode,stdout=completed.stdout[-4096:],stderr=completed.stderr[-8192:],note='Exploratory replay starts with empty pipeline queues and fresh GS state. It does not reproduce all live resumes or verify scene rendering.')
    if data.exists():report['output_data_sha256']=hashlib.sha256(data.read_bytes()).hexdigest()
    if failure.exists():
        sp=importlib.util.spec_from_file_location('xg_probe',GAME/'tests/probe-xgkick-failure.py');xp=importlib.util.module_from_spec(sp);sp.loader.exec_module(xp)
        report['xgkick_failure']=xp.analyze(failure.read_bytes())
    (GAME/'logs/vu-replay-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    completed.check_returncode()
if __name__=='__main__':main()
