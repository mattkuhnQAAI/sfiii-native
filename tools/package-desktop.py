"""Build a portable, offline Windows first-run package containing no game code or assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def external(path):
    path=Path(path).resolve()
    if path==ROOT or ROOT in path.parents:raise ValueError('Packages and dependency cache must stay outside the source repository')
    return path


def read_icon(path):
    data=Path(path).read_bytes()
    if len(data)<6 or data[:4]!=b'\0\0\1\0':raise ValueError('Choose a Windows ICO file')
    count=struct.unpack_from('<H',data,4)[0]
    directory=6+16*count
    if not count or directory>len(data):raise ValueError('Incomplete ICO directory')
    for i in range(count):
        size,offset=struct.unpack_from('<II',data,6+16*i+8)
        if not size or offset<directory or offset+size>len(data):raise ValueError('Incomplete ICO image')
    return data

def extract(archive,destination,spec):
    destination.mkdir(parents=True,exist_ok=False)
    if spec['format']=='keep':
        shutil.copyfile(archive,destination/spec['archive'])
    elif spec['format']=='mame-sfx':
        # Only the independently invoked CD image utility and upstream notices.
        subprocess.run([str(archive.resolve()),'-o'+str(destination.resolve()),'-y',
                        'chdman.exe','COPYING','docs/legal/*'],check=True,stdout=subprocess.DEVNULL)
        if not (destination/'chdman.exe').is_file():raise ValueError('CHD tool missing from official archive')
        # The upstream self-extractor may ignore member filters. Never ship its
        # emulator, sample folders or unrelated utilities with this application.
        for child in destination.iterdir():
            if child.name in {'chdman.exe','COPYING','docs'}:continue
            if destination.resolve() not in child.resolve().parents:raise ValueError('Unsafe extraction cleanup path')
            if child.is_dir():shutil.rmtree(child)
            else:child.unlink()
        for child in (destination/'docs').iterdir():
            if child.name=='legal':continue
            if destination.resolve() not in child.resolve().parents:raise ValueError('Unsafe extraction cleanup path')
            if child.is_dir():shutil.rmtree(child)
            else:child.unlink()
    elif spec['format']=='tar':
        with tarfile.open(archive) as t:t.extractall(destination,filter='data')
    else:
        with zipfile.ZipFile(archive) as z:
            for item in z.infolist():
                target=(destination/item.filename).resolve()
                if destination.resolve() not in target.parents and target!=destination.resolve():raise ValueError('Unsafe dependency archive path')
                if (item.external_attr>>16)&0o170000==0o120000:raise ValueError('Unexpected archive symlink')
            z.extractall(destination)

def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--cache',type=Path,required=True)
    p.add_argument('--zip',action='store_true',help='Also create an external ZIP after building the application')
    p.add_argument('--icon',type=Path,help='Optional local branding ICO outside the source repository')
    a=p.parse_args();out=external(a.out);cache=external(a.cache)
    icon=read_icon(external(a.icon)) if a.icon else None
    if out.exists():raise ValueError('Choose a new package output folder to preserve earlier builds')
    cache.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True)
    payload=out/'payload';app=payload/'app';app.mkdir(parents=True)
    if icon is not None:(payload/'app-icon.ico').write_bytes(icon)
    lock=json.loads((ROOT/'desktop/dependencies.json').read_text())
    upstream=json.loads((ROOT/'dependencies.json').read_text())
    names=sorted(set(subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT).decode().split('\0'))-{''})
    for name in names:
        target=app/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
    # Source dependencies only: no Git metadata, captures, generated programs or local builds.
    runtime=ROOT/'.local/saturn-recomp'
    if subprocess.check_output(['git','-C',str(runtime),'rev-parse','HEAD']).decode().strip()!=upstream['runtime']['commit']:
        raise ValueError('Unexpected upstream runtime revision')
    subprocess.run(['git','-C',str(runtime),'apply','--reverse','--check',str(ROOT/'tools/runtime-diagnostics.patch')],check=True)
    rt=app/'.local/saturn-recomp';rt.mkdir(parents=True)
    for name in ['runtime','src']:
        shutil.copytree(runtime/name,rt/name,ignore=shutil.ignore_patterns('__pycache__','*.pyc','.git'))
    for name in ['LICENSE','THIRD_PARTY.md']:shutil.copyfile(runtime/name,rt/name)
    db=app/'.local/controllerdb';db.mkdir()
    for name in ['gamecontrollerdb.txt','LICENSE']:shutil.copyfile(ROOT/'.local/controllerdb'/name,db/name)
    dependency_hashes={}
    for spec in lock['archives']:
        archive=cache/spec['archive']
        if not archive.exists():
            print('Downloading '+spec['name'],flush=True)
            with urllib.request.urlopen(spec['url']) as response,archive.open('wb') as f:shutil.copyfileobj(response,f)
        if sha(archive)!=spec['sha256']:raise ValueError('Checksum mismatch: '+spec['name'])
        print('Unpacking '+spec['name'],flush=True)
        unpack=out/'_unpack'/spec['name'];extract(archive,unpack,spec)
        source=unpack/spec['strip'] if spec['strip'] else unpack
        shutil.move(str(source),str(payload/spec['name']))
        if spec['strip']:unpack.rmdir()
        dependency_hashes[spec['name']]=spec['sha256']
    (out/'_unpack').rmdir()
    # Isolated Python needs only the packaged standard library and application tools.
    (payload/'python/python314._pth').write_text('python314.zip\n.\n../app/tools\n../app\n',encoding='utf-8')
    shaders=payload/'shaders';shaders.mkdir()
    dxc=ROOT/'.local/dxc-1.9.2609/bin/x64/dxc.exe'
    for name in ['geometry','hd-assets','display-filter']:
        for fmt in ['dxil','spv']:
            command=[str(dxc),'-T','ps_6_0','-E','main','-O3']
            if fmt=='spv':command+=['-spirv','-fspv-target-env=vulkan1.0']
            subprocess.run(command+[str(ROOT/'native'/f'{name}.hlsl'),'-Fo',str(shaders/f'{name}.{fmt}')],check=True)
    csc=Path(os.environ.get('SystemRoot',r'C:\Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    icon_args=['/win32icon:'+str(payload/'app-icon.ico')] if icon is not None else []
    subprocess.run([str(csc),'/nologo','/target:winexe','/platform:x64','/optimize+','/debug-',*icon_args,
                    '/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Web.Extensions.dll',
                    '/win32manifest:'+str(ROOT/'native/app.manifest'),'/out:'+str(out/'SFIII Native.exe'),str(ROOT/'desktop/Launcher.cs')],check=True)
    fingerprint=hashlib.sha256()
    for name in sorted(x.relative_to(payload).as_posix() for x in payload.rglob('*') if x.is_file() and x.suffix not in {'.pyc'}):
        # Large tool binaries are pinned by the dependency archive hashes; hash app/runtime/shaders here.
        if name.split('/')[0] in {'app','shaders'} or name=='app-icon.ico':
            fingerprint.update(name.encode());fingerprint.update(bytes.fromhex(sha(payload/name)))
    fingerprint.update(json.dumps(dependency_hashes,sort_keys=True).encode())
    manifest=dict(version=lock['version'],build_id=fingerprint.hexdigest()[:16],dependencies=dependency_hashes,
                  runtime_commit=upstream['runtime']['commit'],game_data_included=False,generated_game_code_included=False)
    (payload/'package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (out/'START HERE.txt').write_text(
        'Shining Force III Native '+lock['version']+'\n\n'
        '1. Extract the WHOLE ZIP to a local application folder.\n'
        '2. Open SFIII Native.exe.\n'
        '3. Choose the game and browse to your supported CUE or CHD image. Keep CUE BIN tracks beside it.\n'
        '4. Keep or choose a game-data folder outside this application folder, then click Prepare game.\n'
        '5. Wait for preparation, then click Play. No compiler installation, terminal commands or BIOS needed.\n\n'
        'Windows 10/11 x64 with .NET Framework 4.8 and a Vulkan/Direct3D 12 capable GPU.\n'
        'Preparation is offline, can take several minutes, and needs several GB of free space.\n'
        'No game code or assets are included. Keep this application folder: it contains the tools used by setup and save import.\n'
        'Experimental alpha; complete-game compatibility is not certified.\n'
        'Your saves and checkpoints stay in the separate game-data folder.\n'
        'Controller setup and Import saves are available after preparation.\n'
        'This package is unsigned; Windows may show an unknown-publisher prompt.\n',encoding='utf-8')
    shutil.copyfile(ROOT/'LICENSE',out/'LICENSE.txt');shutil.copyfile(ROOT/'THIRD_PARTY.md',out/'THIRD_PARTY.md')
    shutil.copytree(ROOT/'LICENSES',out/'LICENSES')
    for notice in lock.get('licenses',[]):
        file=cache/notice['file']
        if not file.exists():
            with urllib.request.urlopen(notice['url']) as response,file.open('wb') as f:shutil.copyfileobj(response,f)
        if sha(file)!=notice['sha256']:raise ValueError('License checksum mismatch')
        shutil.copyfile(file,out/'LICENSES'/notice['file'])
    # Publish-time inventory of shipped files; generated data can never be an input to this packager.
    inventory={str(x.relative_to(out)).replace('\\','/'):sha(x) for x in out.rglob('*') if x.is_file()}
    (out/'PACKAGE-FILES.json').write_text(json.dumps(inventory,indent=2)+'\n')
    if a.zip:
        result=shutil.make_archive(str(out), 'zip',root_dir=out.parent,base_dir=out.name)
        Path(result+'.sha256').write_text(sha(result)+'  '+Path(result).name+'\n')
        print('Package ZIP: '+result,flush=True)
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':main()
