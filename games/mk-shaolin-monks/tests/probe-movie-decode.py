"""Check the retail intro against the same FFmpeg distribution as the runtime.

This validates the source asset and installed decoder only, not native playback.
"""
import hashlib, json, pathlib, subprocess
GAME=pathlib.Path(__file__).resolve().parents[1]; ROOT=GAME.parents[1]
BIN=ROOT/'out/mk-runtime/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin'
MOVIE=ROOT/'MortalKombatShaolinMonks/Front/Movies/midway.sfd'
def run(name,*args):
    p=subprocess.run([str(BIN/(name+'.exe')),*map(str,args)],capture_output=True,text=True,timeout=30)
    assert p.returncode==0,p.stderr
    return p.stdout
metadata=json.loads(run('ffprobe','-v','error','-show_entries','stream=codec_name,width,height,r_frame_rate',
    '-show_entries','format=format_name,duration','-of','json',MOVIE))
video=next(s for s in metadata['streams'] if s['codec_name']=='mpeg2video')
assert (video['width'],video['height'])==(512,384)
decoded=run('ffmpeg','-v','error','-i',MOVIE,'-map','0:v:0','-frames:v','12','-f','framemd5','-')
frames=[line.split(',') for line in decoded.splitlines() if line and not line.startswith('#')]
assert len(frames)==12 and len({f[-1].strip() for f in frames})>1
report={'asset':str(MOVIE),'sha256':hashlib.sha256(MOVIE.read_bytes()).hexdigest(),
    'metadata':metadata,'decoded_frames':len(frames),'unique_hashes':len({f[-1].strip() for f in frames}),
    'scope':'Standalone decode with runtime FFmpeg distribution; native playback is not verified','passed':True}
(GAME/'logs/movie-decode-probe.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 12 retail intro frames decoded; distinct frame hashes; MPEG-2 512x384 source')
