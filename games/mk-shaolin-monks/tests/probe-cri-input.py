"""Compare a live inspector video-ring prefix with independent FFmpeg demux.

Capture first with --memory-watch 'cri_input=0x105f900:128' and
--memory-watch 'video_es=0xe499c0:256'. Addresses are this retail run's heap
objects, not stable symbols: reject mismatches rather than silently guessing.
"""
import hashlib
import json
import pathlib
import struct
import subprocess

GAME = pathlib.Path(__file__).resolve().parents[1]
ROOT = GAME.parents[1]
capture = json.loads((GAME/'logs/inspector/inspector.json').read_text())
session = json.loads((GAME/'logs/inspector/report.json').read_text())
assert session['session'] == capture['session']
assert session['elf_sha256'] == 'b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'
watches = {w['name']: w for w in capture['watches']}
ring, video = watches['cri_input'], watches['video_es']
assert ring['valid'] and video['valid']
fields = struct.unpack('<32I', bytes.fromhex(ring['bytes']))
assert fields[0] & 0x1fffffff == video['address']
assert 0 < fields[4] <= fields[2] and fields[6] == fields[2]*2048
guest = bytes.fromhex(video['bytes'])
assert len(guest) == 256 and guest[:4] == b'\0\0\x01\xb3'
movie = ROOT/'MortalKombatShaolinMonks/Front/Movies/midway.sfd'
ffmpeg = ROOT/'out/mk-runtime/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin/ffmpeg.exe'
p = subprocess.run([str(ffmpeg), '-v', 'error', '-i', str(movie), '-map', '0:v:0',
                    '-c', 'copy', '-f', 'mpeg2video', '-'], capture_output=True, timeout=30)
assert p.returncode == 0, p.stderr.decode(errors='replace')
assert p.stdout[:len(guest)] == guest, 'Live input differs from independently demuxed video'
report = {'passed': True, 'session': capture['session'], 'captured_unix_ms': capture['captured_unix_ms'],
          'elf_sha256': session['elf_sha256'], 'runner_sha256': session['runner_sha256'],
          'movie_sha256': hashlib.sha256(movie.read_bytes()).hexdigest(),
          'ring_address': hex(ring['address']), 'buffer_address': hex(video['address']),
          'queued_blocks': fields[4], 'block_bytes': 2048, 'matching_prefix_bytes': len(guest),
          'host_mpeg': capture['mpeg'],
          'scope': 'Live guest ES prefix matches independent demux; does not verify the whole ring or native decoding'}
(GAME/'logs/cri-input-probe.json').write_text(json.dumps(report, indent=2)+'\n')
print('PASS: 256 live guest video bytes match independent retail MPEG-2 demux')
