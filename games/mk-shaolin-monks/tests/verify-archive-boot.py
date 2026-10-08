"""Verify startup/read milestones from a PS2X_BOOT_TRACE boot, not playability."""
import argparse
import pathlib
import re
import json

root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('stderr_log', type=pathlib.Path)
args = parser.parse_args()
err = args.stderr_log.read_text(errors='replace')
out = args.stderr_log.with_name(args.stderr_log.name.replace('.stderr.', '.stdout.')).read_text(errors='replace')
assert 'path="\\\\GAMEDATA.WAD;1"' in out and '[sceCdSearchFile:ok]' in out, 'Archive search not observed'
workers = [int(x) for x in re.findall(r'\[mk-thread-create\] result=(-?\d+)', err)]
assert len(workers) == 4 and all(x > 1 for x in workers), f'Worker creation failed: {workers}'
assert re.search(r'threads=5\b', err), 'Five-thread scheduler snapshot not observed'
read = re.search(r'\[mk-sector-read\] lsn=[0-9a-f]+ sectors=1d dest=([0-9a-f]+)\s+\[mk-sector-result\] result=1 bytes=59392 fnv1a=([0-9a-f]+)', err)
assert read, 'Initial successful archive transfer not observed'
with (root.parent.parent/'MortalKombatShaolinMonks/GAMEDATA.WAD').open('rb') as source:
    data = source.read(59392)
assert len(data) == 59392
expected = 2166136261
for value in data:
    expected = ((expected ^ value) * 16777619) & 0xffffffff
assert int(read[2],16) == expected, 'Guest buffer differs from extracted archive'
result = {'log': str(args.stderr_log), 'workers': workers, 'archive_bytes_verified': len(data), 'fnv1a': f'{expected:08x}', 'guest_destination': hex(int(read[1],16)), 'playability_verified': False}
print(json.dumps(result, indent=2))
