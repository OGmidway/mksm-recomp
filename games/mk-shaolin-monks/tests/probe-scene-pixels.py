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
    assert len(rows) <= 1024, 'capture exceeded bound'
    groups = collections.Counter()
    transitions = []
    for row in rows:
        src, dst, result = (rgb(row[name]) for name in ('source', 'destination', 'result'))
        before_white = min(dst) >= 240
        after_white = min(result) >= 240
        if before_white != after_white and len(transitions) < 48:
            transitions.append(dict(row, source_rgb=src, destination_rgb=dst, result_rgb=result))
        if after_white:
            groups[(row['primitive'], row['fbp'], row['tex'], row['tme'], row['fge'],
                    row['alpha'], min(src) >= 240, before_white)] += 1
    result = {'session': report['session'], 'runner_sha256': report['runner_sha256'],
              'capture_sha256': capture['sha256'], 'rows': len(rows),
              'white_write_groups': [{'primitive': k[0], 'fbp': k[1], 'tex': k[2], 'tme': k[3],
                'fge': k[4], 'alpha': k[5], 'source_already_white': k[6],
                'destination_already_white': k[7], 'count': count} for k, count in groups.most_common()],
              'white_transitions': transitions,
              'limitation': 'Two fixed pixels; successful raster writes only. Transfers and rejected fragments are not captured.'}
    (GAME / 'logs/scene-pixels-probe.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k != 'white_transitions'}, indent=2))
    print('first white transitions:', json.dumps(transitions[:4]))


if __name__ == '__main__':
    main()
