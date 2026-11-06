"""Offline preparation worker for the packaged Windows application. No game data is shipped."""
import argparse
import contextlib
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True

APP = Path(__file__).resolve().parents[1]
PAYLOAD = APP.parent
PACKAGE = PAYLOAD.parent
sys.path.insert(0,str(APP/'tools'))
sys.path.insert(0,str(APP/'desktop'))
from disc_profile import identify
from disc_input import resolve_disc
NO_WINDOW = 0x08000000 if os.name == 'nt' else 0

def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()

def status(phase, message, done=0, total=0):
    print('SF3_STATUS '+json.dumps(dict(phase=phase,message=message,done=done,total=total)),flush=True)

def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix+'.part')
    temporary.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    os.replace(temporary,path)

def external_work(path, package=None):
    work = Path(path).resolve()
    package = Path(PACKAGE if package is None else package).resolve()
    if work == package or package in work.parents:
        raise ValueError('Choose a game-data folder outside the application folder.')
    if any((p/'.git').exists() for p in [work,*work.parents]):
        raise ValueError('Choose a game-data folder outside source repositories.')
    return work

def build_environment(payload=PAYLOAD):
    env = {k:v for k,v in os.environ.items() if not k.startswith(('SF3_','PYTHON','CMAKE_'))}
    for k in list(env):
        if k.upper() in {'INCLUDE','LIB','LIBPATH','CC','CXX','CL','_CL_','LINK','CFLAGS','CXXFLAGS','LDFLAGS'} or k.upper().startswith(('VSCMD','VCTOOLS','WINDOWSSDK','VSINSTALL','VCINSTALL')):
            env.pop(k,None)
    windows = Path(env.get('SystemRoot',r'C:\Windows'))
    env['PATH'] = os.pathsep.join(str(x) for x in [payload/'compiler/bin',payload/'cmake/bin',payload/'ninja',windows/'System32',windows])
    env.update(PYTHONUNBUFFERED='1',NINJA_STATUS='[%f/%t] ')
    return env

def run(command, env, log, phase, cwd=None, progress=False):
    command = list(map(str,command))
    if Path(command[0]).name.lower() in {'python.exe','python3','python'}:command[1:1]=['-X','utf8','-B']
    log.write('\nCOMMAND '+json.dumps(list(map(str,command)))+'\n');log.flush()
    proc = subprocess.Popen(list(map(str,command)),env=env,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                            text=True,encoding='utf-8',errors='replace',creationflags=NO_WINDOW)
    for line in proc.stdout:
        log.write(line);log.flush()
        if progress:
            match = re.match(r'\[(\d+)/(\d+)\]',line)
            if match:status(phase,'Preparing the game for this PC...',int(match[1]),int(match[2]))
    if proc.wait():
        raise RuntimeError('Preparation stopped during '+phase+'. See the local setup log for details.')

def checked_disc(cue, app=APP):
    cue = Path(cue).resolve(strict=True)
    if cue.suffix.lower() != '.cue':raise ValueError('Select the CUE file beside your disc tracks.')
    sys.path.insert(0,str(app/'.local/saturn-recomp/src'))
    from saturnrecomp.disc import Disc, manifest
    disc = Disc(str(cue))
    identify(disc,manifest(disc),app/'profiles')
    return cue

def disc_revision(cue, report_dir=None):
    from saturnrecomp.disc import Disc, manifest
    disc = Disc(str(cue))
    contents = manifest(disc)
    try:
        return identify(disc,contents)[1]
    except ValueError as error:
        if report_dir is None:raise
        from disc_profile import content_id
        report = Path(report_dir)/'unregistered-disc.json'
        atomic_json(report,dict(content_sha256=content_id(contents),disc_header=disc.ip.describe(),
                               manifest=contents,status='unregistered; native compatibility not established'))
        raise ValueError(str(error)+' Local identification report: '+str(report)) from error

@contextlib.contextmanager
def preparation_lock(work):
    # OS lock releases even after Cancel terminates the worker. The file may remain.
    if os.name != 'nt':
        import fcntl
        with (work/'preparation.lock').open('a+b') as file:
            try:fcntl.flock(file,fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:raise ValueError('Another preparation is already using this game-data folder.')
            yield
        return
    import msvcrt
    lock = (work/'preparation.lock').open('a+b')
    if lock.seek(0,2) == 0:lock.write(b'0');lock.flush()
    lock.seek(0)
    try:
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:raise ValueError('Another preparation is already using this game-data folder.')
        yield
    finally:lock.close()

def ensure_game_stopped(work):
    # Inspect only application processes; never modify another running installation.
    script = "$r=$env:SF3_PREPARE_WORK; Get-CimInstance Win32_Process -Filter \"Name='saturn.exe'\" | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($r+'\\',[StringComparison]::OrdinalIgnoreCase)} | ForEach-Object {$_.ProcessId}"
    ps = Path(os.environ.get('SystemRoot',r'C:\Windows'))/'System32/WindowsPowerShell/v1.0/powershell.exe'
    result = subprocess.run([str(ps),'-NoProfile','-Command',script],env={**os.environ,'SF3_PREPARE_WORK':str(work),'PSModulePath':str(ps.parent/'Modules')},capture_output=True,text=True,creationflags=NO_WINDOW)
    if result.returncode:raise RuntimeError('Could not check for a running game. Close the game and retry.')
    if result.stdout.strip():raise ValueError('Close the game before preparing this data folder.')

def prepare(cue, work, scenario=None):
    manifest = json.loads((PAYLOAD/'package.json').read_text())
    build_id = manifest['build_id']
    work = external_work(work)
    work.mkdir(parents=True,exist_ok=True)
    ensure_game_stopped(work)
    with preparation_lock(work):
        status('identify','Checking your disc. Your files stay on this PC...')
        source = Path(cue).resolve(strict=True)
        with (work/'disc-input.log').open('w',encoding='utf-8') as disc_log:
            cue = resolve_disc(source,work,PAYLOAD/'chdman/chdman.exe',disc_log,status)
        cue = checked_disc(cue)
        profile = disc_revision(cue,work)
        if scenario is not None and profile['scenario'] != scenario:
            raise ValueError('The selected image belongs to Scenario '+str(profile['scenario'])+'. Choose that game in the launcher first.')
        if (work/'game.json').exists():
            previous = json.loads((work/'game.json').read_text())
            old_profile = disc_revision(previous['cue'])
            if old_profile['id'] != profile['id']:
                raise ValueError('This folder belongs to another game revision. Choose a separate game-data folder to keep its saves and checkpoints safe.')
        build_key = build_id+'-'+profile['id']
        stage = work/'preparation'/build_key
        stage.mkdir(parents=True,exist_ok=True)
        build = work/('build-desktop-'+build_key)
        python = PAYLOAD/'python/python.exe'
        cmake = PAYLOAD/'cmake/bin/cmake.exe'
        compiler = PAYLOAD/'compiler/bin'
        env = build_environment()
        with (stage/'setup.log').open('w',encoding='utf-8') as log:
            status('inventory','Reading your disc and preparing local game files...')
            run([python,APP/'tools/probe.py','--cue',cue,'--framework',APP/'.local/saturn-recomp','--out',stage],env,log,'inventory')
            status('audit','Preparing game programs...')
            revision = json.loads((stage/'revision.json').read_text())
            run([python,APP/'tools/audit-modules.py','--work',stage,'--profile',APP/'profiles'/revision['profile'],'--out',stage/'module-audit'],env,log,'audit')
            status('generate','Preparing native game code. This can take a few minutes...')
            generated = stage/'recomp-bulk'
            run([python,APP/'tools/generate.py','--framework','saturn-recomp','--work',stage,'--profile',stage/'module-audit/candidate-profile.json','--out',generated],env,log,'generate',cwd=APP)
            run([python,APP/'tools/check-module-build.py','--audit',stage/'module-audit/module-audit.json','--generated',generated,'--out',stage/'emission.json','--player'],env,log,'emission')
            status('configure','Setting up the game for this PC...')
            run([cmake,'-S',generated,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_C_COMPILER='+(compiler/'x86_64-w64-mingw32-clang.exe').as_posix(),
                 '-DCMAKE_CXX_COMPILER='+(compiler/'x86_64-w64-mingw32-clang++.exe').as_posix(),
                 '-DCMAKE_EXE_LINKER_FLAGS=-static','-DSATURN_VDP1_GPL=OFF',
                 '-DSATURN_RUNTIME='+(APP/'.local/saturn-recomp/runtime').as_posix(),'-DSATURN_EXTRA='+(APP/'tools/windows.cmake').as_posix(),
                 '-DCMAKE_PREFIX_PATH='+(PAYLOAD/'sdl/x86_64-w64-mingw32').as_posix(),
                 '-DSF3_PRECOMPILED_SHADERS='+(PAYLOAD/'shaders').as_posix(),
                 '-DSF3_APP_ICON='+((PAYLOAD/'app-icon.ico').as_posix() if (PAYLOAD/'app-icon.ico').is_file() else '')],env,log,'configure')
            status('build','Preparing the game for this PC...')
            run([cmake,'--build',build,'--target','saturn','controller_setup','controller_selftest','replay_checkpoint','--parallel',str(min(4,max(1,os.cpu_count() or 1)))],env,log,'build',progress=True)
            shutil.copyfile(PAYLOAD/'sdl/x86_64-w64-mingw32/bin/SDL3.dll',build/'SDL3.dll')
            status('check','Checking the prepared game...')
            smoke = stage/'boot-check';smoke.mkdir(exist_ok=True)
            run([build/'saturn.exe','--headless','--cue',cue,'--save',smoke/'backup.bin','--settings','-',
                 '--vblanks',str(profile.get('boot_check_vblanks',120)),'--out',smoke,'--log',smoke/'run.log'],env,log,'boot-check')
            # Never copy test backups into live progress. Activate only after a successful build/check.
            status('install','Finishing setup...')
            for name in ['controller_setup.exe']:
                shutil.copyfile(build/name,work/'controller-setup.exe')
            shutil.copyfile(build/'SDL3.dll',work/'SDL3.dll')
            for name in ['run-native.ps1','publish-replay-save.ps1','Launch Native SFIII.cmd','Load Replay Checkpoint.cmd','Load Native State.cmd',
                         'Controller Setup.cmd','Controller Test.cmd','Import Saturn Saves.cmd','import-saturn-save.ps1','import-saturn-save.py']:
                shutil.copyfile(APP/'tools'/name,work/name)
            db = APP/'.local/controllerdb'
            shutil.copyfile(db/'gamecontrollerdb.txt',work/'gamecontrollerdb.txt')
            shutil.copyfile(db/'LICENSE',work/'CONTROLLER-DATABASE-LICENSE.txt')
            old = (work/'native-build.txt').read_text().strip() if (work/'native-build.txt').exists() else None
            atomic_json(stage/'activation.json',dict(previous_build=old,new_build=build.name,live_backups_replaced=False))
            atomic_json(work/'game.json',dict(cue=str(cue),source=str(source),profile_id=profile['id']))
            (work/'save-import-python.txt').write_text(str(python),encoding='utf-8')
            shutil.copyfile(APP/'desktop/ai_packs.py',work/'ai-packs-helper.py')
            (work/'ai-profiles').mkdir(exist_ok=True)
            for artwork_profile in (APP/'profiles').glob('*.json'):
                shutil.copyfile(artwork_profile,work/'ai-profiles'/artwork_profile.name)
            selection = work/'native-build.txt.part';selection.write_text(build.name,encoding='utf-8');os.replace(selection,work/'native-build.txt')
            atomic_json(work/'desktop-ready.json',dict(build_id=build_id,build_directory=build.name,profile_id=profile['id'],title=profile['title'],executable_sha256=sha(build/'saturn.exe'),cue=str(cue),source=str(source),version=manifest['version']))
        status('ready','Ready to play.')

def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--cue',type=Path,required=True);p.add_argument('--work',type=Path,required=True)
    p.add_argument('--scenario',type=int,choices=[1,2,3])
    a = p.parse_args()
    try:prepare(a.cue,a.work,a.scenario);return 0
    except Exception as error:
        status('error',str(error));return 1

if __name__ == '__main__':raise SystemExit(main())
