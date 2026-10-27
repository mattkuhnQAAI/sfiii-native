"""Offline Linux/macOS preparation. The package supplies the compiler and tools."""
import contextlib
import json
import os
from pathlib import Path
import platform
import shutil
import sys

sys.dont_write_bytecode = True
import prepare as common

APP, PAYLOAD = common.APP, common.PAYLOAD


def package_root(payload=PAYLOAD):
    return next((p for p in payload.parents if p.suffix == '.app'), payload.parent)


@contextlib.contextmanager
def lock(work, name='runtime.lock'):
    import fcntl
    work.mkdir(parents=True, exist_ok=True)
    with (work/name).open('a+b') as file:
        try:
            fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('Close the game or other setup/import process for this folder first.')
        try:
            yield
        finally:
            fcntl.flock(file, fcntl.LOCK_UN)


def environment(payload=PAYLOAD):
    env = {k:v for k,v in os.environ.items() if not k.startswith(('SF3_', 'PYTHON', 'CMAKE_', 'DYLD_', 'LD_'))}
    for key in ['CC','CXX','CFLAGS','CXXFLAGS','LDFLAGS','SDKROOT','CPATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH']:
        env.pop(key, None)
    env.update(PATH=os.pathsep.join(map(str, [payload/'compiler/bin', payload/'cmake/bin', payload/'ninja', '/usr/bin', '/bin'])),
               PYTHONUNBUFFERED='1', PYTHONDONTWRITEBYTECODE='1', NINJA_STATUS='[%f/%t] ',
               ZIG_GLOBAL_CACHE_DIR=str(Path.home()/'.cache/sfiii-native/zig'))
    return env


def configure_args(generated, build, payload=PAYLOAD):
    rpath = '@executable_path/lib' if sys.platform == 'darwin' else '$ORIGIN/lib'
    args = [payload/'cmake/bin/cmake', '-S', generated, '-B', build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DSATURN_VDP1_GPL=OFF',
            '-DCMAKE_C_COMPILER='+str(payload/'compiler/bin/cc'),
            '-DCMAKE_CXX_COMPILER='+str(payload/'compiler/bin/c++'),
            '-DCMAKE_AR='+str(payload/'compiler/bin/ar'),
            '-DCMAKE_RANLIB='+str(payload/'compiler/bin/ranlib'),
            '-DCMAKE_MAKE_PROGRAM='+str(payload/'ninja/ninja'),
            # CMake may detect a system ld.lld and pass --dependency-file,
            # which the bundled Zig linker rejects.
            '-DCMAKE_LINK_DEPENDS_USE_LINKER=OFF',
            '-DSATURN_RUNTIME='+str(payload/'app/.local/saturn-recomp/runtime'),
            '-DSATURN_EXTRA='+str(payload/'app/tools/windows.cmake'),
            '-DCMAKE_PREFIX_PATH='+str(payload/'sdl'),
            '-DSF3_PRECOMPILED_SHADERS='+str(payload/'shaders'),
            '-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON', '-DCMAKE_INSTALL_RPATH='+rpath]
    if sys.platform == 'darwin':args.append('-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0')
    return args


def prepare(source, work, scenario):
    manifest = json.loads((PAYLOAD/'package.json').read_text())
    arch = {'arm64':'aarch64','AMD64':'x86_64'}.get(platform.machine(), platform.machine())
    if manifest['platform'] != sys.platform or manifest['arch'] != arch:
        raise ValueError('This application package is for another operating system or CPU architecture.')
    work = common.external_work(work, package_root())
    with lock(work):
        source = Path(source).resolve(strict=True)
        common.status('identify', 'Checking your disc locally...')
        with (work/'disc-input.log').open('w', encoding='utf-8') as log:
            cue = common.resolve_disc(source, work, PAYLOAD/'chdman/chdman', log, common.status)
        common.checked_disc(cue)
        profile = common.disc_revision(cue,work)
        if profile['scenario'] != scenario:
            raise ValueError('The disc does not match the selected scenario.')
        if (work/'game.json').exists():
            previous = json.loads((work/'game.json').read_text())
            previous_id=previous.get('profile_id')
            if not previous_id:
                previous_id=common.disc_revision(previous['cue'])['id']
            if previous_id != profile['id']:
                raise ValueError('Use a separate data folder for each scenario and disc revision.')
        key = manifest['build_id']+'-'+profile['id']
        stage, build = work/'preparation'/key, work/('build-desktop-'+key)
        stage.mkdir(parents=True, exist_ok=True)
        python, cmake = PAYLOAD/'python/bin/python3', PAYLOAD/'cmake/bin/cmake'
        env = environment()
        with (stage/'setup.log').open('w', encoding='utf-8') as log:
            common.status('inventory', 'Reading your disc...')
            common.run([python,APP/'tools/probe.py','--cue',cue,'--framework',APP/'.local/saturn-recomp','--out',stage],env,log,'inventory')
            common.status('audit', 'Mapping game programs...')
            revision = json.loads((stage/'revision.json').read_text())
            common.run([python,APP/'tools/audit-modules.py','--work',stage,'--profile',APP/'profiles'/revision['profile'],'--out',stage/'module-audit'],env,log,'audit')
            generated = stage/'recomp-bulk'
            common.status('generate', 'Generating native code locally...')
            common.run([python,APP/'tools/generate.py','--framework','saturn-recomp','--work',stage,'--profile',stage/'module-audit/candidate-profile.json','--out',generated],env,log,'generate',cwd=APP)
            common.run([python,APP/'tools/check-module-build.py','--audit',stage/'module-audit/module-audit.json','--generated',generated,'--out',stage/'emission.json','--player'],env,log,'emission')
            common.status('configure', 'Configuring bundled tools...')
            common.run(configure_args(generated,build),env,log,'configure')
            common.status('build', 'Building your game. No manual compiler setup is needed...')
            common.run([cmake,'--build',build,'--target','saturn','controller_setup','controller_selftest','replay_checkpoint','--parallel',str(min(4,os.cpu_count() or 1))],env,log,'build',progress=True)
            shutil.copytree(PAYLOAD/'sdl/lib', build/'lib', dirs_exist_ok=True, symlinks=True)
            smoke = stage/'boot-check'
            smoke.mkdir(exist_ok=True)
            common.status('check', 'Checking startup...')
            common.run([build/'saturn','--headless','--cue',cue,'--save',smoke/'backup.bin','--settings','-',
                        '--vblanks',str(profile.get('boot_check_vblanks',120)),'--out',smoke,'--log',smoke/'run.log'],env,log,'boot-check')
            # Never seed ordinary progress with boot-check or historical replay data.
            old = (work/'native-build.txt').read_text().strip() if (work/'native-build.txt').exists() else None
            common.atomic_json(stage/'activation.json',dict(previous_build=old,new_build=build.name,live_backups_replaced=False))
            shutil.copyfile(APP/'.local/controllerdb/gamecontrollerdb.txt',work/'gamecontrollerdb.txt')
            common.atomic_json(work/'game.json',dict(cue=str(cue),source=str(source),profile_id=profile['id']))
            temporary = work/'native-build.txt.part'
            temporary.write_text(build.name)
            os.replace(temporary, work/'native-build.txt')
            common.atomic_json(work/'desktop-ready.json',dict(build_id=manifest['build_id'],build_directory=build.name,
                               executable_sha256=common.sha(build/'saturn'),profile_id=profile['id'],cue=str(cue),
                               source=str(source),version=manifest['version'],platform=sys.platform,arch=arch))
        common.status('ready', 'Ready to play. Gameplay compatibility on this platform remains experimental.')


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--cue',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--scenario',type=int,choices=[1,2,3],required=True)
    a = p.parse_args()
    try:
        prepare(a.cue,a.work,a.scenario)
    except Exception as e:
        common.status('error',str(e))
        sys.exit(1)
