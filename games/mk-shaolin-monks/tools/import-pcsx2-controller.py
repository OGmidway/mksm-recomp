"""Import standard PCSX2 SDL/XInput gamepad bindings into the recomp profile.

Reads only Pad sections, never changes PCSX2. Raw devices, chords and mixed
devices within one port are rejected instead of guessing their meaning.
"""
import argparse,configparser,io,json,math,pathlib,re
GAME=pathlib.Path(__file__).resolve().parents[1]
TARGETS='Select L3 R3 Start Up Right Down Left L2 R2 L1 R1 Triangle Circle Cross Square LLeft LRight LUp LDown RLeft RRight RUp RDown'.split()
ALIASES={'FaceSouth':'South','A':'South','Cross':'South','FaceEast':'East','B':'East','Circle':'East',
         'FaceWest':'West','X':'West','Square':'West','FaceNorth':'North','Y':'North','Triangle':'North',
         'LB':'LeftShoulder','RB':'RightShoulder','L3':'LeftStick','R3':'RightStick',
         'Up':'DPadUp','Down':'DPadDown','Left':'DPadLeft','Right':'DPadRight'}
BUTTONS=set('South East West North Back Start LeftStick RightStick LeftShoulder RightShoulder DPadUp DPadRight DPadDown DPadLeft'.split())
AXES=set('LeftX LeftY RightX RightY LeftTrigger RightTrigger'.split())

def binding(value):
    if not value.strip():return None,'None'
    m=re.fullmatch(r'(?:SDL|XInput)-(\d+)/([+-]?\w+)',value.strip())
    if not m:raise ValueError('Unsupported binding: '+value)
    device=int(m[1]);raw=m[2];sign=raw[0] if raw[0] in '+-' else '';raw=raw.lstrip('+-')
    raw=ALIASES.get(raw,raw)
    if not 0<=device<4 or raw not in BUTTONS|AXES or sign and raw in BUTTONS:raise ValueError('Unsupported binding: '+value)
    return device,sign+raw

def convert(text):
    lines=[];keep=False
    for line in text.splitlines():
        if line.strip().startswith('['):keep=line.strip() in ('[Pad1]','[Pad2]')
        if keep:lines.append(line)
    config=configparser.ConfigParser(interpolation=None);config.optionxform=str;config.read_string('\n'.join(lines))
    out=configparser.ConfigParser(interpolation=None);out.optionxform=str;notes=[];ports=[]
    for section in ('Pad1','Pad2'):
        if section not in config:continue
        src=config[section]
        if src.get('Type','DualShock2') not in ('DualShock2','None'):raise ValueError('Unsupported pad type in '+section)
        if src.get('Type')=='None':out[section]={'Device':'-1'};continue
        if not any(re.match(r'(SDL|XInput)-',src.get(k,'')) for k in TARGETS):
            notes.append(section+': no standard gamepad bindings; recomp defaults retained');continue
        dst={};devices=set()
        for key in TARGETS:
            dev,value=binding(src.get(key,''));dst[key]=value
            if dev is not None:devices.add(dev)
        if len(devices)!=1:raise ValueError(section+' uses multiple devices; map one controller per port')
        dst={'Device':str(devices.pop()),**dst}
        for key,default,low,high,target in [('Deadzone',0,0,.95,'Deadzone'),('AxisScale',1.33,.01,2,'AxisScale'),('ButtonDeadzone',0,0,1,'TriggerThreshold')]:
            value=float(src.get(key,str(default)))
            if not math.isfinite(value) or not low<=value<=high:raise ValueError('Out-of-range '+key)
            dst[target]=str(value)
        for stick in ('L','R'):
            invert=int(src.get('Invert'+stick,'0'))
            if not 0<=invert<=3:raise ValueError('Unsupported axis inversion')
            if invert&1:dst[stick+'Left'],dst[stick+'Right']=dst[stick+'Right'],dst[stick+'Left']
            if invert&2:dst[stick+'Up'],dst[stick+'Down']=dst[stick+'Down'],dst[stick+'Up']
        if src.get('UseDiagonalScaleCorrection','false').lower()=='true':raise ValueError('Diagonal scale correction is not supported by this importer')
        extra=[k for k in ('Analog','Pressure','LargeMotor','SmallMotor') if src.get(k)]
        if extra:notes.append(section+': not imported: '+', '.join(extra))
        out[section]=dst;ports.append(section)
    if not ports:raise ValueError('No standard gamepad mappings found')
    stream=io.StringIO();out.write(stream)
    return '# Imported standard PCSX2 gamepad bindings. Device numbers are connected-controller order.\n'+stream.getvalue(),{'ports':ports,'notes':notes}

def self_test():
    for token,expected in [('SDL-0/FaceSouth',(0,'South')),('SDL-0/A',(0,'South')),('XInput-1/-LeftX',(1,'-LeftX')),('SDL-2/+LeftTrigger',(2,'+LeftTrigger'))]:assert binding(token)==expected
    for token in ('SDL-0/JoyButton99','DInput-0/A','SDL-0/A & SDL-0/B','SDL-4/A','Keyboard/X'):
        try:binding(token)
        except ValueError:pass
        else:raise AssertionError(token)
    text,report=convert('[Pad1]\nCross=SDL-0/FaceSouth\nLLeft=SDL-0/-LeftX\nLRight=SDL-0/+LeftX\nInvertL=1\n[Pad2]\nCross=Keyboard/K\n')
    assert 'Cross = South' in text and 'LLeft = +LeftX' in text and 'Circle = None' in text and report['ports']==['Pad1']
    print('PASS: modern/legacy SDL, XInput, unbound controls, inversion and unsupported binding rejection')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=pathlib.Path,nargs='?');p.add_argument('--output',type=pathlib.Path,default=GAME/'config/controllers.ini');p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test();return
    if not a.source:p.error('Provide a PCSX2.ini or input-profile INI')
    if a.source.resolve()==a.output.resolve():p.error('Output must differ from PCSX2 source')
    if a.source.stat().st_size>1024*1024:p.error('Source exceeds 1 MiB')
    text,report=convert(a.source.read_text(encoding='utf-8-sig'));a.output.write_text(text,encoding='utf-8')
    report.update(source=str(a.source),output=str(a.output))
    (GAME/'logs/controller-import.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
