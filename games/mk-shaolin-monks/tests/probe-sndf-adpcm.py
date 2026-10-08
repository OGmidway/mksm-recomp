"""Decode the first retail SNDF sample and produce bounded native PCM fixtures."""
import hashlib, importlib.util, json, struct, subprocess
from pathlib import Path

GAME=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sndf',GAME/'tests/probe-sndf-init.py')
sndf=importlib.util.module_from_spec(spec);spec.loader.exec_module(sndf)
COEFFICIENTS=((0,0),(60,0),(115,-52),(98,-55),(122,-60))

def decode(data):
    history=[0,0];samples=[]
    for pos in range(0,len(data),16):
        block=data[pos:pos+16];assert len(block)==16
        shift=block[0]&15;filter_=block[0]>>4
        assert filter_<5
        a,b=COEFFICIENTS[filter_]
        for byte in block[2:]:
            for nibble in (byte&15,byte>>4):
                signed=nibble-16 if nibble&8 else nibble
                value=(signed*4096>>shift)+((a*history[0]+b*history[1]+32)>>6)
                value=max(-32768,min(32767,value))
                history=[value,history[0]];samples.append(value)
    return samples

def main():
    bank=sndf.retail_bank(36);start=0x461b0;blocks=812
    data=bank[start:start+blocks*16]
    assert data[-15]==1 and all(data[i+1]==0 for i in range(0,len(data)-16,16))
    samples=decode(data);pcm=struct.pack('<'+str(len(samples))+'h',*samples)
    # FFmpeg uses different predictor rounding/history than SPU2. Compare it
    # as a separate decoder, not as a bit-exact hardware reference.
    vag=bytearray(48);vag[:4]=b'VAGp';struct.pack_into('>III',vag,8,0,len(data),48000)
    exe=GAME.parents[1]/'out/mk-runtime/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin/ffmpeg.exe'
    ref=subprocess.run([str(exe),'-v','error','-f','vag','-i','pipe:0','-f','s16le','pipe:1'],input=bytes(vag)+data,capture_output=True,check=True).stdout
    assert len(ref)==len(pcm)
    reference=struct.unpack('<'+str(len(samples))+'h',ref)
    differences=[x-y for x,y in zip(samples,reference)]
    fixture=b'SADP'+struct.pack('<II',len(data),len(samples))+data+pcm
    assert len(fixture)<64*1024
    (GAME/'logs/sndf-adpcm-fixture.bin').write_bytes(fixture)
    report={'bank_sha256':hashlib.sha256(bank).hexdigest(),'sample_offset':hex(start),'blocks':blocks,'samples':len(samples),
            'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'peak':max(abs(v) for v in samples),
            'ffmpeg_comparison':{'equal_samples':sum(v==0 for v in differences),'maximum_difference':max(abs(v) for v in differences),
                'rms_difference':(sum(v*v for v in differences)/len(differences))**0.5},
            'reference':'https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp',
            'scope':'Raw ADPCM decoding only; no envelope, interpolation, mixing, device playback or game completion claim'}
    (GAME/'logs/sndf-adpcm-probe.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
