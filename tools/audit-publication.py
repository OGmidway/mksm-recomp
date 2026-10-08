"""Check Git's indexed bytes for excluded content and obvious credentials.
This is a publication guard, not a substitute for reviewing the source diff.
"""
import ast, json, pathlib, re, subprocess, sys, tomllib
ROOT=pathlib.Path(__file__).resolve().parents[1]
def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT)
def main():
    paths=git('ls-files','-z').decode().split('\0');paths=[p for p in paths if p]
    errors=[];total=0
    banned={'.iso','.elf','.wad','.sfd','.irx','.bin','.exe','.dll','.lib','.pdb','.zip','.rar','.7z','.pcm','.wav','.suprx','.key','.pem'}
    private=re.compile(r'(^|/)(logs|MortalKombatShaolinMonks|MKSMprototype|out|build|kernel-output|pr244-output|staging-output)(/|$)|runtime/output/|runtime/pr244-selected/.*\.inc$|config/.*\.local\.toml$',re.I)
    secrets=re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|AKIA[0-9A-Z]{16})')
    for name in paths:
        p=pathlib.PurePosixPath(name);data=git('show',':'+name);total+=len(data)
        if p.suffix.lower() in banned or private.search(name) or name.lower().startswith('ghidra/') or p.name.upper().startswith('SLUS_'):errors.append((name,'excluded file'))
        if data.startswith((b'\x7fELF',b'MZ',b'Rar!',b'PK\x03\x04')):errors.append((name,'binary executable/archive signature'))
        if secrets.search(data):errors.append((name,'credential pattern'))
        try:
            text=data.decode('utf-8-sig')
            if re.search(r'[A-Za-z]:[\\/]Users[\\/]danny',text,re.I):errors.append((name,'personal absolute path'))
            if p.suffix=='.py':ast.parse(text,filename=name)
            elif p.suffix=='.json':json.loads(text)
            elif p.suffix=='.toml':tomllib.loads(text)
        except (ValueError,SyntaxError,UnicodeError) as ex:errors.append((name,'text/parse error: '+str(ex).splitlines()[0]))
    for name,reason in errors:print(name+': '+reason)
    print(f'Indexed files: {len(paths)}; bytes: {total}; findings: {len(errors)}')
    return 1 if errors or not paths else 0
if __name__=='__main__':sys.exit(main())
