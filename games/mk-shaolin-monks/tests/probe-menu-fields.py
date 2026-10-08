"""Validate the captured retail menu field shift before changing presentation."""
from pathlib import Path
import collections,csv,itertools,json
GAME=Path(__file__).resolve().parents[1]
source=GAME/'logs/primitive-motion.csv'
rows=[r for r in csv.DictReader(source.open()) if r['primitive'] in ('3','4','5')]
groups=[list(g) for _,g in itertools.groupby(rows,key=lambda r:r['fbp'])]
assert len(groups)>=4
transitions=[]
for left,right in zip(groups,groups[1:]):
    assert len(left)==len(right)==738, 'Capture must be the stationary autosave warning'
    dx={round(float(v['x'])-float(u['x']),4) for u,v in zip(left,right)}
    dy=collections.Counter(round(float(v['y'])-float(u['y']),4) for u,v in zip(left,right))
    assert dx=={0.0}
    delta=0.5 if right[0]['fbp']=='0' else -0.5
    assert dy=={delta:732,0.0:6}
    transitions.append({'from_fbp':int(left[0]['fbp']),'to_fbp':int(right[0]['fbp']),'dy_counts':dict(dy)})
report={'vertex_count_per_field':738,'field_transitions':transitions,
        'retail_vertex_shift_pc':'0x37e4e8','source':'ghidra/retail-menu-projection.txt',
        'baseline_warning_mean_pixel_difference':12.737010785824346}
path=GAME/'logs/menu-field-diagnosis.json'
if path.exists():
    saved=json.loads(path.read_text())
    report.update({k:v for k,v in saved.items() if k.endswith('_verification') or k=='runner_sha256'})
(GAME/'logs/menu-field-diagnosis.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 732/738 vertices alternate by half a row; x and the remaining six vertices stay fixed')
