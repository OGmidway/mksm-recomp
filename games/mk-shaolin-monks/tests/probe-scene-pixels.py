"""Summarize hash-bound, bounded GS pixel provenance without guessing a fix."""
import collections
import csv
import hashlib
import json
import pathlib

GAME = pathlib.Path(__file__).resolve().parents[1]
RUN = GAME / 'logs/first-stage-relinked'


def rgb(value):
    return [value & 255, (value >> 8) & 255, (value >> 16) & 255]


def main():
    report = json.loads((RUN / 'report.json').read_text())
    capture = report['pixel_capture']
    data = (RUN / 'pixels.csv').read_bytes()
    assert capture['session'] == report['session'], 'stale capture session'
    assert hashlib.sha256(data).hexdigest() == capture['sha256'], 'capture hash mismatch'
    # Initial trace build emitted the one-byte TCC field as a character.
    rows = [{key: (ord(value) if key == 'tcc' and value in ('\x00', '\x01') else int(value)) for key, value in row.items()}
            for row in csv.DictReader(data.decode().splitlines())]
    assert len(rows) <= 8192, 'capture exceeded bound'
    # Track framebuffer continuity separately for each observed address. A gap
    # means another writer (e.g. a transfer/clear) acted between sampled writes.
    previous = {}
    gaps = []
    first_white = {}
    depth_writes = collections.Counter()
    for row in rows:
        key = (row['fbp'], row['psm'], row['x'], row['y'])
        if key in previous and previous[key] != row['destination'] and len(gaps) < 24:
            gaps.append({'write':row['write'], 'key':key, 'previous':previous[key], 'destination':row['destination']})
        previous[key] = row['result']
        if min(rgb(row['result'])) >= 240:
            first_white.setdefault(str(key), row)
        if row.get('write_depth'):
            depth_writes[(row['primitive'],row['zbp'],row['zpsm'],row['z']==0)] += 1
    rejected=[r for r in rows if r.get('zpass')==0]
    assert all(r['destination']==r['result'] and not r['write_depth'] for r in rejected)
    depth_clear=[r for r in rows if r.get('write_depth') and r['z']==0]
    first_rejected={}
    for row in rejected:
        first_rejected.setdefault(str((row['fbp'],row['x'],row['y'])),row)
    groups = collections.Counter()
    transitions = []
    for row in rows:
        src, dst, result = (rgb(row[name]) for name in ('source', 'destination', 'result'))
        before_white = min(dst) >= 240
        after_white = min(result) >= 240
        if before_white != after_white and len(transitions) < 48:
            transitions.append(dict(row, source_rgb=src, destination_rgb=dst, result_rgb=result))
        if after_white and row.get('zpass',1):
            groups[(row['primitive'], row['fbp'], row['tex'], row['tme'], row['fge'],
                    row['alpha'], min(src) >= 240, before_white)] += 1
    result = {'session': report['session'], 'runner_sha256': report['runner_sha256'],
              'capture_sha256': capture['sha256'], 'rows': len(rows),
              'white_write_groups': [{'primitive': k[0], 'fbp': k[1], 'tex': k[2], 'tme': k[3],
                'fge': k[4], 'alpha': k[5], 'source_already_white': k[6],
                'destination_already_white': k[7], 'count': count} for k, count in groups.most_common()],
              'rejected_depth_fragments':len(rejected), 'first_rejected_by_pixel':first_rejected,
              'depth_zero_write_count':len(depth_clear),'first_depth_zero_writes':depth_clear[:8],
              'white_transitions': transitions, 'first_white_by_pixel':first_white,
              'unobserved_writer_gaps':gaps,
              'depth_write_groups':[{'primitive':k[0],'zbp':k[1],'zpsm':k[2],'zero_depth':k[3],'count':v} for k,v in depth_writes.items()],
              'limitation': 'Two fixed pixels; alpha/discard rejection and transfer/clear helper writes are not captured. zpass is present only in newer captures. Pixel-format conversion can explain continuity gaps outside CT32.'}
    (GAME / 'logs/scene-pixels-probe.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k not in ('white_transitions','first_white_by_pixel','unobserved_writer_gaps','first_rejected_by_pixel','first_depth_zero_writes')}, indent=2))
    print('first white transitions:', json.dumps(transitions[:4]))


if __name__ == '__main__':
    main()
