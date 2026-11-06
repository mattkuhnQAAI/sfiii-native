"""Validate bundled tools and synthetic runtime cases; never claim game coverage."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'desktop'))
import prepare_posix as worker


def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    package=a.package.resolve()
    resources=package/'Contents/Resources' if package.suffix=='.app' else package
    payload=resources/'payload'
    out=a.out.resolve()
    if out==ROOT or ROOT in out.parents or out==package or package in out.parents:raise ValueError('Use a new external test directory.')
    out.mkdir(parents=True,exist_ok=False)
    python,cmake=payload/'python/bin/python3',payload/'cmake/bin/cmake'
    env=worker.environment(payload)
    env['SDL_AUDIODRIVER']='dummy'
    home,temp=out/'test-home',out/'test-temp'
    home.mkdir();temp.mkdir()
    env.update(HOME=str(home),TMPDIR=str(temp),TMP=str(temp),TEMP=str(temp))
    results=[]
    def check(name,args,extra=None,required=True,expected=0):
        print('CHECK '+name,flush=True)
        with (out/(name+'.log')).open('wb') as log:
            r=subprocess.run(list(map(str,args)),env={**env,**(extra or {})},stdout=log,stderr=subprocess.STDOUT,timeout=1200,cwd=out)
        text=(out/(name+'.log')).read_text(errors='replace')
        result=dict(name=name,exit_code=r.returncode,expected_exit_code=expected,passed=r.returncode==expected)
        results.append(result)
        print(('PASS ' if result['passed'] else 'FAIL ')+name,flush=True)
        if required and not result['passed']:raise RuntimeError(name+' failed:\n'+text[-6000:])
        return result,text
    check('python-tests',[python,'-X','utf8','-B','-m','unittest','discover','-s',payload/'app/tools/tests','-p','test_*.py'])
    # Native tools must not depend on maintainer-only Homebrew or checkout paths.
    command=['otool','-L'] if sys.platform=='darwin' else ['ldd']
    _,text=check('chd-library-dependencies',[*command,payload/'chdman/chdman'])
    dependencies='\n'.join(text.splitlines()[1:]) if sys.platform=='darwin' else text
    if 'not found' in dependencies:
        raise RuntimeError('The CHD converter has an unbundled build-machine dependency:\n'+text)
    for line in dependencies.splitlines():
        target=line.split(' => ',1)[-1].strip().rsplit(' (',1)[0]
        bundled=target.startswith('/') and Path(target).resolve().is_relative_to(package)
        if not bundled and any(prefix in target for prefix in ['/opt/homebrew/','/usr/local/','/Users/','/home/runner/']):
            raise RuntimeError('The CHD converter has an unbundled build-machine dependency:\n'+line)
    if sys.platform=='linux':
        if 'libSDL2' in dependencies:raise RuntimeError('The CHD converter still requires unbundled SDL2.')
        sdl=next((line.split(' => ',1)[1].rsplit(' (',1)[0] for line in dependencies.splitlines() if 'libSDL3.so' in line and ' => ' in line),None)
        if not sdl or not Path(sdl).resolve().is_relative_to(payload/'sdl'):
            raise RuntimeError('The CHD converter did not resolve the bundled SDL3 library.')
        _,rpath=check('chd-runtime-path',['patchelf','--print-rpath',payload/'chdman/chdman'])
        if rpath.strip()!='$ORIGIN/../sdl/lib':raise RuntimeError('The CHD converter runtime path is not relocatable.')
    # Invented mixed data/audio sectors exercise the actual converter, never a game.
    disc=out/'synthetic disc é'
    disc.mkdir()
    size=2352*600
    invented=(bytes(range(256))*((size+255)//256))[:size]
    (disc/'source.bin').write_bytes(invented)
    (disc/'source.cue').write_text('FILE "source.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 00:04:00\n')
    converter=payload/'chdman/chdman'
    check('chd-create',[converter,'createcd','-i',disc/'source.cue','-o',disc/'synthetic.chd'])
    check('chd-info',[converter,'info','-i',disc/'synthetic.chd'])
    check('chd-verify',[converter,'verify','-i',disc/'synthetic.chd'])
    check('chd-extract',[converter,'extractcd','-i',disc/'synthetic.chd','-o',disc/'output.cue','-ob',disc/'output.bin'])
    if (disc/'output.bin').read_bytes()!=invented:
        raise RuntimeError('Synthetic CHD data/audio sectors changed during the round trip.')
    # Hide SDK discovery from the bundled compiler, proving this does not rely on Xcode setup.
    compiler_env={}
    if sys.platform=='darwin':
        compiler_env['DEVELOPER_DIR']=str(out/'no-xcode')
        blocker=out/'no-sdk-bin'
        blocker.mkdir()
        (blocker/'xcrun').write_text('#!/bin/sh\nexit 1\n')
        (blocker/'xcrun').chmod(0o755)
        compiler_env['PATH']=str(blocker)+os.pathsep+env['PATH']
    runtime=payload/'app/.local/saturn-recomp/runtime'
    checks=out/'checks'
    configure=[cmake,'-S',payload/'app/tools/tests','-B',checks,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
               '-DCMAKE_C_COMPILER='+str(payload/'compiler/bin/cc'),'-DCMAKE_CXX_COMPILER='+str(payload/'compiler/bin/c++'),
               '-DCMAKE_AR='+str(payload/'compiler/bin/ar'),'-DCMAKE_RANLIB='+str(payload/'compiler/bin/ranlib'),
               '-DCMAKE_MAKE_PROGRAM='+str(payload/'ninja/ninja'),'-DSATURN_RUNTIME='+str(runtime)]
    check('configure-regressions',configure,compiler_env)
    gates=['dsp_regression','core_regression','bup_regression','machine_regression','background_regression','audio_buffer']
    check('build-regressions',[cmake,'--build',checks,'--target',*gates,'--parallel','2'],compiler_env)
    for gate in gates:check(gate,[checks/gate],required=False)
    source=out/'synthetic'
    # The upstream instruction fixtures are original synthetic code. Exercise the
    # real emitter and its generated CMake, with native-state safepoints enabled.
    generator='''import sys
sys.path.insert(0,sys.argv[1])
from saturnrecomp.recomp.__main__ import generate
generate([],sys.argv[2],optest=True,native_states=True)
'''
    check('synthetic-aot-generation',[python,'-X','utf8','-B','-c',generator,payload/'app/.local/saturn-recomp/src',source])
    # ISA vectors never request snapshots. Fail if a snapshot is requested here;
    # the real machine/state tests cover serialization separately.
    (source/'state-stub.cpp').write_text('#include "saturn/sh2.h"\n#include <cstdlib>\nstd::atomic<bool> g_state_requested{false};\nvoid state_native_point(SH2Context&,uint32_t){std::abort();}\n')
    with (source/'CMakeLists.txt').open('a') as f:
        f.write('\ntarget_sources(selftest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/state-stub.cpp")\n')
    build=out/'synthetic-build'
    check('configure-native-host',worker.configure_args(source,build,payload),compiler_env)
    check('build-native-host',[cmake,'--build',build,'--target','saturn','selftest','controller_setup','controller_selftest','replay_checkpoint','gpu_renderer_selftest','gpu_parity','replay_ui_selftest','native_state_ui_selftest','--parallel','2'],compiler_env)
    shutil.copytree(payload/'sdl/lib',build/'lib',symlinks=True)
    check('synthetic-aot-instructions',[build/'selftest',source/'selftest/optest.txt'],required=False)
    # The current runtime prints its usage and exits 2 for --help.
    _,usage=check('native-cli-usage',[build/'saturn','--help'],expected=2)
    if 'usage: saturn --cue' not in usage:raise RuntimeError('Native host usage was not printed.')
    check('controller-input',[build/'controller_selftest'],{'SDL_VIDEODRIVER':'dummy'})
    ui=out/'ui'
    seed=ui/'seed'
    seed.mkdir(parents=True)
    for name,data in {'context.json':'{}','build-id.txt':'synthetic','disc-id.txt':'synthetic','backup.bin':''}.items():(seed/name).write_text(data)
    ui_env=dict(SDL_VIDEODRIVER='dummy',SF3_CONTROLLER_DIR=str(ui),SF3_REPLAY_ROOT=str(ui/'points'),SF3_REPLAY_SEED=str(seed))
    check('replay-ui',[build/'replay_ui_selftest',ui/'replay'],ui_env)
    check('native-state-ui',[build/'native_state_ui_selftest',ui/'native-states'],ui_env)
    entry=package/'Contents/MacOS/SFIII Native' if package.suffix=='.app' else package/'SFIII Native'
    check('launcher-ui',[entry,'--self-test',out/'launcher'])
    gpu,text=check('gpu-shader-cases',[build/'gpu_renderer_selftest',out/'gpu',build/'shaders'],
                   {'SF3_GPU_BACKEND':'metal' if sys.platform=='darwin' else 'vulkan','SF3_GPU_ALLOW_SOFTWARE':'1'},False)
    if not gpu['passed']:
        if any(s in text for s in ['GPU renderer initialization','SDL video initialization']):
            gpu['status']='unavailable: this runner exposes no usable GPU/display'
        else:raise RuntimeError('GPU shader test failed:\n'+text[-6000:])
    else:gpu['status']='synthetic shader cases passed; not game/hardware validation'
    # GPU against the accurate CPU path on synthetic VDP1/VDP2 scenes at 1x/2x/4x.
    # Diff PNGs stay in the validation directory, uploaded as a workflow artifact.
    parity,text=check('gpu-cpu-parity',[build/'gpu_parity',out/'gpu-parity',build/'shaders'],
                      {'SF3_GPU_BACKEND':'metal' if sys.platform=='darwin' else 'vulkan','SF3_GPU_ALLOW_SOFTWARE':'1'},False)
    if not parity['passed']:
        if not gpu['passed'] and any(s in text for s in ['GPU renderer initialization','SDL video initialization']):
            parity['status']='unavailable: this runner exposes no usable GPU/display'
        else:raise RuntimeError('GPU/CPU parity test failed:\n'+text[-6000:])
    else:parity['status']='synthetic scenes within CPU-parity limits; 1x exact'
    # Move the whole application, then use its actual executable and compiler.
    relocated=out/'relocated application é'/package.name
    relocated.parent.mkdir()
    package.rename(relocated)
    try:
        relative_resources=resources.relative_to(package)
        moved_payload=relocated/relative_resources/'payload'
        moved_entry=relocated/entry.relative_to(package)
        check('relocated-launcher-ui',[moved_entry,'--self-test',out/'relocated-launcher'])
        check('relocated-chd-converter',[moved_payload/'chdman/chdman','info','-i',disc/'synthetic.chd'])
        probe=out/'relocation-source'
        probe.mkdir()
        (probe/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(relocation CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
add_executable(relocation main.cpp)
target_link_libraries(relocation PRIVATE SDL3::SDL3 Threads::Threads)
''')
        (probe/'main.cpp').write_text('#include <SDL3/SDL.h>\n#include <thread>\n#include <filesystem>\nint main(){std::jthread worker([]{});return SDL_GetVersion()==0 || std::filesystem::current_path().empty();}\n')
        moved_build=out/'relocation-build'
        check('relocated-compiler-configure',worker.configure_args(probe,moved_build,moved_payload),compiler_env)
        check('relocated-compiler-build',[moved_payload/'cmake/bin/cmake','--build',moved_build],compiler_env)
        shutil.copytree(moved_payload/'sdl/lib',moved_build/'lib',symlinks=True)
        check('relocated-native-host',[moved_build/'relocation'])
    finally:
        relocated.rename(package)
    inventory=json.loads((resources/'PACKAGE-FILES.json').read_text())
    changed=[name for name,digest in inventory.items() if not (resources/name).is_file() or worker.common.sha(resources/name)!=digest]
    added={p.relative_to(resources).as_posix() for p in resources.rglob('*') if p.is_file()}-set(inventory)-{'PACKAGE-FILES.json'}
    changed.extend('added: '+name for name in sorted(added))
    results.append(dict(name='package-files-unchanged',passed=not changed,changed_files=changed))
    if changed:raise RuntimeError('Validation changed packaged resources: '+', '.join(changed[:10]))
    # GPU checks raise on a real failure above; an unavailable GPU (no display on the runner) is not a synthetic failure.
    report=dict(synthetic_passed=all(r['passed'] for r in results if r['name'] not in ('gpu-shader-cases','gpu-cpu-parity')),
                gameplay_validated=False,transition_fixtures='unavailable: game data is never uploaded to CI',
                physical_controller_audio_validation='unavailable',results=results)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
