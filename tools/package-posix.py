"""Build self-contained Linux/macOS candidates on their native OS, without game data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]


def metal_binding_flags(name):
    # Match SDL's uniform-first buffer table, rather than automatic assignment.
    return ['--msl-decoration-binding'] if name=='geometry' else []


def validate_metal_bindings(code,name):
    if 'fragmentMain' not in code or '[[buffer(0)]]' not in code or '[[buffer(1)]]' not in code:
        raise ValueError('Metal shader resource mapping was not verified.')
    if name=='geometry':
        for variable,kind,slot in [('Pass','buffer',0),('scene','buffer',1),('textures','buffer',2),
                                   ('picture','texture',0),('picture_sampler','sampler',0)]:
            pattern=r'\b'+variable+r'\s*\[\['+kind+r'\('+str(slot)+r'\)\]\]'
            if not re.search(pattern,code):
                raise ValueError('Metal geometry shader binding does not match SDL: '+variable)


def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def external(path):
    path=Path(path).resolve()
    if path==ROOT or ROOT in path.parents:raise ValueError('Outputs and caches must be outside the source checkout.')
    return path


def run(args,**kwargs):
    print('RUN '+' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True,**kwargs)


def git_source(cache,name,spec):
    out=cache/name
    if not out.exists():
        out.mkdir()
        run(['git','init',out])
        run(['git','-C',out,'remote','add','origin',spec['url']])
        run(['git','-C',out,'fetch','--depth','1','origin',spec['commit']])
        run(['git','-C',out,'checkout','--detach','FETCH_HEAD'])
    actual=subprocess.check_output(['git','-C',str(out),'rev-parse','HEAD'],text=True).strip()
    if actual!=spec['commit']:raise ValueError('Dependency checkout changed: '+name)
    if subprocess.check_output(['git','-C',str(out),'diff','--name-only']).strip():raise ValueError('Modified dependency: '+name)
    return out


def unpack(cache,name,spec):
    archive=cache/(name+'-'+spec['sha256'][:16]+'.archive')
    if not archive.exists():
        print('Downloading '+name,flush=True)
        with urllib.request.urlopen(spec['url']) as response,archive.open('wb') as out:shutil.copyfileobj(response,out)
    if sha(archive)!=spec['sha256']:raise ValueError('Archive checksum mismatch: '+name)
    destination=cache/(name+'-'+spec['sha256'][:16])
    if not destination.exists():
        destination.mkdir()
        with tarfile.open(archive) as t:t.extractall(destination,filter='data')
    return destination


def bootstrap(cache):
    upstream=json.loads((ROOT/'dependencies.json').read_text())
    for name in ['runtime','controllers']:
        spec=upstream[name]
        destination=ROOT/'.local'/spec['directory']
        if not destination.exists():
            destination.parent.mkdir(exist_ok=True)
            run(['git','clone','--no-checkout',spec['url'],destination])
            run(['git','-C',destination,'checkout','--detach',spec['commit']])
        if name=='runtime':
            patch=ROOT/'tools/runtime-diagnostics.patch'
            applied=subprocess.run(['git','-C',str(destination),'apply','--reverse','--check',str(patch)],capture_output=True).returncode==0
            if not applied:run(['git','-C',destination,'apply',patch])
        if subprocess.check_output(['git','-C',str(destination),'rev-parse','HEAD'],text=True).strip()!=spec['commit']:
            raise ValueError('Wrong upstream source revision.')


def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cache',type=Path,required=True)
    p.add_argument('--shaders',type=Path,required=True,help='Project-owned SPIR-V from the shader CI job')
    a=p.parse_args()
    if sys.platform not in {'linux','darwin'}:raise ValueError('Build this package on Linux or macOS.')
    arch={'arm64':'aarch64'}.get(platform.machine(),platform.machine())
    name=sys.platform+'-'+arch
    lock=json.loads((ROOT/'desktop/posix-dependencies.json').read_text())
    specs=lock['platforms'][name]
    out,cache=external(a.out),external(a.cache)
    out.mkdir(parents=True,exist_ok=False)
    cache.mkdir(parents=True,exist_ok=True)
    bootstrap(cache)
    package=out/('SFIII Native.app' if sys.platform=='darwin' else 'SFIII Native')
    resources=package/'Contents/Resources' if sys.platform=='darwin' else package
    payload=resources/'payload'
    app=payload/'app'
    app.mkdir(parents=True)
    files=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT,text=True).split('\0')
    for rel in filter(None,files):
        target=app/rel
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/rel,target)
    for name in ['runtime','src']:
        shutil.copytree(ROOT/'.local/saturn-recomp'/name,app/'.local/saturn-recomp'/name,ignore=shutil.ignore_patterns('__pycache__','*.pyc','.git'))
    for name in ['LICENSE','THIRD_PARTY.md']:
        shutil.copy2(ROOT/'.local/saturn-recomp'/name,app/'.local/saturn-recomp'/name)
    shutil.copytree(ROOT/'.local/controllerdb',app/'.local/controllerdb',ignore=shutil.ignore_patterns('.git'))
    for component in ['python','zig','cmake']:
        source=next(unpack(cache,component,specs[component]).iterdir())
        if component=='cmake' and sys.platform=='darwin':source=source/'CMake.app/Contents'
        shutil.copytree(source,payload/component,symlinks=True)
    compiler=payload/'compiler/bin'
    compiler.mkdir(parents=True)
    for driver,mode in [('cc','cc'),('c++','c++'),('ar','ar'),('ranlib','ranlib')]:
        script=compiler/driver
        script.write_text('#!/bin/sh\nexec "$(dirname "$0")/../../zig/zig" '+mode+' "$@"\n')
        script.chmod(0o755)
    cmake=payload/'cmake/bin/cmake'
    sources={n:git_source(cache,n,spec) for n,spec in lock['git'].items()}
    jobs=str(min(4,os.cpu_count() or 1))
    prefix=payload/'sdl'
    # MAME's pkg-config parser cannot handle spaces in library paths. Install
    # once into the external cache, then relocate SDL into the player package.
    sdl_install=cache/'sdl-install' if sys.platform=='linux' else prefix
    build=cache/'build-sdl'
    run([cmake,'-S',sources['sdl'],'-B',build,'-DCMAKE_BUILD_TYPE=Release','-DSDL_SHARED=ON','-DSDL_STATIC=OFF',
         '-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDL_FRAMEWORK=OFF','-DCMAKE_INSTALL_NAME_DIR=@rpath','-DCMAKE_INSTALL_PREFIX='+str(sdl_install)])
    run([cmake,'--build',build,'--parallel',jobs])
    run([cmake,'--install',build])
    if sdl_install!=prefix:shutil.copytree(sdl_install,prefix,symlinks=True)
    for pc in (prefix/'lib/pkgconfig').glob('*.pc'):
        text=pc.read_text()
        pc.write_text('\n'.join('prefix=${pcfiledir}/../..' if line.startswith('prefix=') else line for line in text.splitlines())+'\n')
    run([cmake,'-S',sources['ninja'],'-B',cache/'build-ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF'])
    run([cmake,'--build',cache/'build-ninja','--parallel',jobs])
    (payload/'ninja').mkdir()
    shutil.copy2(cache/'build-ninja/ninja',payload/'ninja/ninja')
    shaders=payload/'shaders'
    shutil.copytree(a.shaders,shaders)
    if sys.platform=='darwin':
        cross=cache/'build-cross'
        run([cmake,'-S',sources['spirv-cross'],'-B',cross,'-DCMAKE_BUILD_TYPE=Release','-DSPIRV_CROSS_ENABLE_TESTS=OFF'])
        run([cmake,'--build',cross,'--parallel',jobs])
        for name in ['geometry','hd-assets']:
            run([cross/'spirv-cross',shaders/(name+'.spv'),'--msl','--msl-version','20100',*metal_binding_flags(name),'--rename-entry-point','main','fragmentMain','frag','--output',shaders/(name+'.msl')])
            code=(shaders/(name+'.msl')).read_text()
            validate_metal_bindings(code,name)
    # Build only the disc conversion utility. No emulator binary enters the package.
    osd='mac' if sys.platform=='darwin' else 'sdl3'
    project='build/projects/'+osd+'/mame/gmake-'+('osx-clang' if sys.platform=='darwin' else 'linux')
    options=['OSD='+osd,'TOOLS=1','EMULATOR=0','REGENIE=1','USE_QTDEBUG=0','NO_USE_PORTAUDIO=1',
             'NO_USE_PULSEAUDIO=1','NO_OPENGL=1','USE_TAPTUN=0','USE_PCAP=0','IGNORE_GIT=1']
    # The top-level makefile generates projects; tool targets live in that project.
    tool_env={**os.environ}
    if sys.platform=='linux':tool_env['PKG_CONFIG_PATH']=str(sdl_install/'lib/pkgconfig')+os.pathsep+tool_env.get('PKG_CONFIG_PATH','')
    run(['make','-C',sources['mame'],'-j'+jobs,*options,'generate',project+'/Makefile'],env=tool_env)
    run(['make','-C',sources['mame']/project,'-j'+jobs,'config=release64','chdman'],env=tool_env)
    (payload/'chdman').mkdir()
    shutil.copy2(sources['mame']/'chdman',payload/'chdman/chdman')
    if sys.platform=='linux':run(['patchelf','--set-rpath','$ORIGIN/../sdl/lib',payload/'chdman/chdman'])
    licenses=resources/'LICENSES'
    licenses.mkdir()
    for name,source in [('SDL',sources['sdl']/'LICENSE.txt'),('Ninja',sources['ninja']/'COPYING'),('SPIRV-Cross',sources['spirv-cross']/'LICENSE'),('MAME',sources['mame']/'COPYING')]:
        shutil.copy2(source,licenses/(name+'.txt'))
    shutil.copytree(sources['mame']/'docs/legal',licenses/'MAME-legal')
    # Corresponding pinned source for the separately invoked CHD tool travels with it.
    run(['git','-C',sources['mame'],'archive','--format=tar.gz','--prefix=mame/','-o',licenses/'MAME-source.tar.gz','HEAD'])
    shutil.copy2(ROOT/'LICENSE',resources/'LICENSE.txt')
    shutil.copy2(ROOT/'THIRD_PARTY.md',resources/'THIRD_PARTY.md')
    dependencies=dict(platform=sys.platform,arch=arch,archives=specs,git=lock['git'])
    h=hashlib.sha256()
    for file in sorted([f for f in app.rglob('*') if f.is_file()]+[f for f in shaders.iterdir() if f.is_file()]):
        h.update(file.relative_to(payload).as_posix().encode())
        h.update(bytes.fromhex(sha(file)))
    h.update(json.dumps(dependencies,sort_keys=True).encode())
    manifest=dict(version=lock['version'],platform=sys.platform,arch=arch,build_id=h.hexdigest()[:16],dependencies=dependencies,
                  gameplay_validation='unavailable on this platform: no game fixtures shipped or uploaded',game_data_included=False,generated_game_code_included=False)
    (payload/'package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    script='#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
    if sys.platform=='darwin':
        executable=package/'Contents/MacOS/SFIII Native'
        executable.parent.mkdir()
        script+='BASE="$HERE/../Resources"\n'
        plist=dict(CFBundleExecutable='SFIII Native',CFBundleIdentifier='org.sfiii.native.preview',CFBundleName='SFIII Native',
                   CFBundleVersion=lock['version'].split('-')[0],CFBundleShortVersionString=lock['version'].split('-')[0],CFBundlePackageType='APPL',LSMinimumSystemVersion='13.0',NSHighResolutionCapable=True)
        with (package/'Contents/Info.plist').open('wb') as f:plistlib.dump(plist,f)
    else:
        executable=package/'SFIII Native.sh'
        script+='BASE="$HERE"\n'
    script+='exec "$BASE/payload/python/bin/python3" -X utf8 -B "$BASE/payload/app/desktop/launcher_posix.py" "$@"\n'
    executable.write_text(script)
    executable.chmod(0o755)
    bootstrap_exe=executable if sys.platform=='darwin' else package/'SFIII Native'
    minimum_os=['-mmacosx-version-min=13.0'] if sys.platform=='darwin' else []
    run([payload/'compiler/bin/cc',ROOT/'native/launcher-bootstrap.c',*minimum_os,'-O2','-o',bootstrap_exe])
    (resources/'START HERE.txt').write_text('Shining Force III Native '+lock['version']+'\n\nOpen the SFIII Native application/launcher. Choose your supported Scenario 1 USA or Scenario 2 translated-v24 CUE/CHD and an external data folder. Click Prepare game, then Play.\n\nThe compiler, Python, SDL, CMake, Ninja and CHD converter are bundled. No game data or BIOS is included. Preparation stays local.\n\nExperimental platform candidate: native unit/build checks are separate from gameplay validation. Real-disc gameplay and hardware GPU/controller/audio checks on this OS remain unvalidated unless specifically documented. Keep backups of saves; native states are build-specific.\n',encoding='utf-8')
    inventory={f.relative_to(resources).as_posix():sha(f) for f in resources.rglob('*') if f.is_file()}
    (resources/'PACKAGE-FILES.json').write_text(json.dumps(inventory,indent=2)+'\n')
    (out/'package-path.txt').write_text(str(package))
    print(json.dumps(manifest),flush=True)


if __name__=='__main__':main()
