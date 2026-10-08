"""Portable MKSM build entry point. Game files and generated code stay local."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
GAME=ROOT/'games/mk-shaolin-monks'
EXPECTED='b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2'

def run(argv, log=None):
    env=os.environ.copy()
    if os.name=='nt':
        value=next((v for k,v in env.items() if k.lower()=='path'),'')
        env={k:v for k,v in env.items() if k.lower()!='path'};env['Path']=value
    print('Running:', ' '.join(map(str,argv)),flush=True)
    if log:
        with log.open('w',encoding='utf-8') as out:
            p=subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT)
        if p.returncode:raise RuntimeError(f'Command failed ({p.returncode}); see {log}')
    else:subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,check=True)

def binary(build,module,name,config):
    suffix='.exe' if os.name=='nt' else ''
    choices=[build/module/config/(name+suffix),build/module/(name+suffix)]
    return next((p for p in choices if p.exists()),choices[0])

def generate(args):
    elf=args.elf.resolve()
    if not elf.is_file():raise RuntimeError(f'Missing retail ELF: {elf}')
    if hashlib.sha256(elf.read_bytes()).hexdigest()!=EXPECTED:raise RuntimeError('Wrong ELF revision; expected NTSC SLUS_210.87 with documented SHA-256')
    recomp=args.recompiler or binary(ROOT/'out/tools','ps2xRecomp','ps2_recomp',args.config)
    if not recomp.is_file():raise RuntimeError('Build the recompiler first: python tools/project.py tools')
    (GAME/'logs').mkdir(exist_ok=True)
    (GAME/'runtime/output').mkdir(parents=True,exist_ok=True)
    stages={'main':('recompile.toml','output',None),'kernel':('kernel.toml','kernel-output','kernel-functions.csv'),'pr244':('pr244-recompile.toml','pr244-output','pr244-functions.csv')}
    for name,(template,output,map_name) in stages.items():
        if args.only!='all' and name!=args.only:continue
        out=GAME/'runtime'/output;out.mkdir(parents=True,exist_ok=True)
        text=(GAME/'config'/template).read_text(encoding='utf-8')
        values={'input':elf.as_posix(),'output':out.as_posix()+'/','ghidra_output':(GAME/'config'/map_name).as_posix() if map_name else ''}
        for key,value in values.items():text=re.sub(r'(?m)^'+key+r' = .*$',lambda _:key+' = '+json.dumps(value),text)
        config=GAME/'config'/(name+'.local.toml');config.write_text(text,encoding='utf-8')
        run([recomp,config],GAME/'logs'/('generate-'+name+'.log'))
        if name=='kernel':
            registration=(out/'register_functions.cpp').read_text(encoding='utf-8')
            bindings=re.findall(r'g_ps2RecompiledFunctionTable\[\d+\] = (mk_\w+); // (0x[0-9a-fA-F]+)',registration)
            if not bindings:raise RuntimeError('No supplemental entry bindings generated')
            names=sorted({n for n,_ in bindings})
            lines=['// Generated locally from the user-provided retail ELF.','#include "game_overrides.h"','#include <stdexcept>']
            lines += [f'void {n}(uint8_t*, R5900Context*, PS2Runtime*);' for n in names]
            lines += [f'#include "../kernel-output/{n}.cpp"' for n in names]
            lines += ['namespace { void installMkKernelEntries(PS2Runtime& runtime) {']
            lines += [f'if (!runtime.replaceFunction({a}u, {n})) throw std::runtime_error("Missing supplemental entry");' for n,a in bindings]
            lines += ['} }','PS2_REGISTER_GAME_OVERRIDE("Shaolin Monks supplemental entries", "SLUS_210.87", 0x0011C070u, 0x1A37A67Cu, installMkKernelEntries)']
            (GAME/'runtime/kernel-hooks.cpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
        if name=='pr244':
            (GAME/'runtime/pr244-selected').mkdir(exist_ok=True)
            run([sys.executable,GAME/'tools/generate-pr244-supplement.py'])
    for source,dest in [('loading-trace.cpp','zz_mk_loading_trace.cpp'),('kernel-hooks.cpp','zz_mk_kernel_hooks.cpp')]:
        if (GAME/'runtime'/source).exists():shutil.copyfile(GAME/'runtime'/source,GAME/'runtime/output'/dest)
    print('Local generation complete. Generated code is ignored by Git.')

def build(args,kind):
    builddir=ROOT/'out'/('tools' if kind=='tools' else 'mk-runtime')
    configure=['cmake','-S',ROOT,'-B',builddir,'-DPS2X_BUILD_STUDIO=OFF','-DPS2X_BUILD_TEST=OFF','-DCMAKE_BUILD_TYPE='+args.config]
    if args.generator:configure+=['-G',args.generator]
    if kind=='tools':
        configure+=['-DPS2X_BUILD_RUNTIME=OFF','-DPS2X_BUILD_RECOMP=ON','-DPS2X_BUILD_ANALYZER=ON'];targets=['ps2_recomp','ps2_analyzer']
    else:
        if not (GAME/'runtime/output/register_functions.cpp').exists():raise RuntimeError('Generate retail code before building the game')
        configure+=['-DPS2X_BUILD_RUNTIME=ON','-DPS2X_BUILD_RECOMP=OFF','-DPS2X_BUILD_ANALYZER=OFF','-DPS2X_ENABLE_FFMPEG=ON','-DPS2X_ENABLE_DEBUG_UI=ON','-DPS2X_BUILD_MKSM_DEBUG_CHECK=ON','-DPS2X_ENABLE_RUNTIME_LOGS=ON','-DPS2X_GENERATED_CODE_DIR='+str(GAME/'runtime/output'),'-DPS2X_DEFAULT_BOOT_ELF='+str(args.elf.resolve())];targets=['ps2EntryRunner']
    run(configure)
    run(['cmake','--build',builddir,'--config',args.config,'--target',*targets,'--parallel',str(args.jobs)])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['check','tools','generate','runtime','all','run']);p.add_argument('--elf',type=pathlib.Path,default=ROOT/'MortalKombatShaolinMonks/SLUS_210.87');p.add_argument('--recompiler',type=pathlib.Path);p.add_argument('--config',default='Release');p.add_argument('--jobs',type=int,default=2);p.add_argument('--generator');p.add_argument('--only',choices=['all','main','kernel','pr244'],default='all');args=p.parse_args()
    if args.jobs<1:p.error('--jobs must be positive')
    if args.action=='check':
        missing=[name for name in ('git','cmake') if not shutil.which(name)]
        print('Python:',sys.version.split()[0]);print('Missing tools:',missing or 'none');print('Retail ELF:',args.elf)
        if args.elf.exists():
            digest=hashlib.sha256(args.elf.read_bytes()).hexdigest();print('ELF SHA-256:',digest)
            if digest!=EXPECTED:raise RuntimeError('Unexpected ELF revision')
        else:print('Retail ELF not installed; game generation/run unavailable, source-only tests remain available.')
        if missing:raise RuntimeError('Install missing build tools')
    if args.action in ('tools','all'):build(args,'tools')
    if args.action in ('generate','all'):generate(args)
    if args.action in ('runtime','all'):build(args,'runtime')
    if args.action=='run':
        exe=binary(ROOT/'out/mk-runtime','ps2xRuntime','ps2EntryRunner',args.config)
        if not exe.exists():raise RuntimeError('Build the runtime first')
        env=os.environ.copy();env.setdefault('PS2_CONTROLLER_CONFIG',str(GAME/'config/controllers.ini'))
        subprocess.run([str(exe),str(args.elf.resolve())],cwd=GAME/'runtime',env=env,check=True)
if __name__=='__main__':main()
