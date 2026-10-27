"""Check the exact Git source selection before publishing; never scan ignored game data."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {'.bin', '.cue', '.iso', '.chd', '.wav', '.sav', '.state', '.exe', '.dll',
             '.obj', '.o', '.lib', '.pdb', '.png', '.jpg', '.zip', '.7z', '.log'}
ALLOWED_TOP = {'native', 'profiles', 'tools', 'docs', 'LICENSES', 'desktop'}
ALLOWED_ROOT = {'.gitignore', '.gitattributes', '.github/FUNDING.yml', '.github/workflows/portable.yml', '.github/workflows/synthetic.yml', '.github/workflows/linux-native.yml', 'README.md', 'LICENSE', 'THIRD_PARTY.md', 'dependencies.json'}
SOURCE_SUFFIXES = {'.c', '.cpp', '.h', '.hlsl', '.inc', '.py', '.ps1', '.cmd', '.cmake', '.patch', '.cs', '.manifest', '.rc'}
PATTERNS = {
    'personal Windows path': re.compile(rb'[A-Za-z]:[\\/]+Users[\\/]+', re.I),
    'GitHub credential': re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})'),
    'private key': re.compile(rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----'),
}

def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--tracked', action='store_true', help='Check the index/committed source, excluding untracked files')
    p.add_argument('--out', type=Path, help='Optional external report path')
    a = p.parse_args()
    command = ['git', 'ls-files', '-z', '--cached']
    if not a.tracked:
        command += ['--others', '--exclude-standard']
    names = sorted(set(subprocess.check_output(command, cwd=ROOT).decode().split('\0')) - {''})
    failures = []
    for name in names:
        path = Path(name)
        if path.parts[0] not in ALLOWED_TOP and name not in ALLOWED_ROOT:
            failures.append([name, 'not in publication source directories'])
        if path.parts[0] in {'native','tools'} and path.suffix.lower() not in SOURCE_SUFFIXES and path.name != 'CMakeLists.txt':
            failures.append([name,'not a source file'])
        if path.parts[0] == 'profiles' and name not in {'profiles/usa-v1.001.json','profiles/scenario1-v25_1.json','profiles/scenario2-v24.json','profiles/scenario3-v25_1.json','profiles/scenario2-v25_1-upgrade.json','profiles/scenario2-supplied-v1003.json'}:
            failures.append([name,'unreviewed generation profile'])
        if path.parts[0] == 'desktop' and name not in {'desktop/dependencies.json','desktop/posix-dependencies.json'} and path.suffix not in {'.py','.cs'}:
            failures.append([name,'not desktop application source'])
        if path.parts[0] == 'docs' and name not in {'docs/BUILDING.md','docs/USAGE.md','docs/DEVELOPMENT.md','docs/DESKTOP.md','docs/POSIX.md'}:
            failures.append([name,'local history or unreviewed documentation'])
        if path.suffix.lower() in FORBIDDEN or any(x in {'.local','research','sessions','captures','generated','build','data','extract'} for x in path.parts):
            failures.append([name, 'game/build/debug artifact path'])
        # With --tracked inspect the exact index blob, not a potentially different working file.
        data = subprocess.check_output(['git','show',':'+name],cwd=ROOT) if a.tracked else (ROOT/path).read_bytes()
        if b'\0' in data:
            failures.append([name, 'binary file'])
        if len(data) > 2_000_000:
            failures.append([name, 'unexpectedly large source file'])
        for label, pattern in PATTERNS.items():
            if pattern.search(data):
                failures.append([name,label])
    report = {'passed':bool(names) and not failures,'files':len(names),'failures':failures,
              'scope':'Selected source only; excluded local fixtures were not read or uploaded.'}
    if a.out:
        out = a.out.resolve()
        if out == ROOT or ROOT in out.parents:
            raise ValueError('Write publication reports outside the repository')
        out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
