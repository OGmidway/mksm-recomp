"""Independent controller reference cases and bundled device-map audit."""
from pathlib import Path
import configparser,json,math,re
GAME=Path(__file__).resolve().parents[1];ROOT=GAME.parents[1]
c=configparser.ConfigParser();c.read(GAME/'config/controllers.ini')
assert c['Pad1']['Cross']=='South' and c['Pad1']['LUp']=='-LeftY'
assert float(c['Pad1']['AxisScale'])==1.33
database=(ROOT/'out/tools/_deps/raylib-src/src/external/glfw/src/mappings.h').read_text()
devices={}
for guid,name,south in [('030000004c050000e60c000000000000','DualSense','b1'),
                        ('030000004c050000c405000000000000','DualShock4','b1'),
                        ('030000005e040000ff02000000000000','XboxOne','b0')]:
    line=next(x for x in database.splitlines() if guid in x)
    assert 'a:'+south+',' in line and 'platform:Windows' in line
    devices[name]={'guid':guid,'south_button':south,'scope':'bundled mapping, not a physical device test'}
def sticks(x,y,deadzone,scale):
    if math.hypot(x,y)<=deadzone:return [128,128]
    return [math.floor((max(-1,min(1,v*scale))+1)*127.5+.5) for v in (x,y)]
assert sticks(0,0,.12,1)==[128,128]
assert sticks(.1,.05,.12,1)==[128,128]
assert sticks(-1,1,0,1)==[0,255]
assert sticks(.5,-.5,0,1.33)==[212,43]
rows=[]
for dz,scale in [(0,1.33),(.12,1),(.2,.75)]:
    for x,y in [(0,0),(.1,.05),(-1,1),(.5,-.5),(1,0),(0,-1)]:
        rows.append((dz,scale,x,y,*sticks(x,y,dz,scale)))
(GAME/'tests/controller_profile_cases.inc').write_text(''.join('{'+','.join(str(float(v))+'f' if i<4 else str(v) for i,v in enumerate(row))+'},\n' for row in rows))
(GAME/'logs/controller-profile-probe.json').write_text(json.dumps({'devices':devices,'stick_cases':rows,'passed':True},indent=2)+'\n')
print('PASS: imported profile, Xbox/DualShock4/DualSense device maps, 18 deadzone/sensitivity reference cases')
