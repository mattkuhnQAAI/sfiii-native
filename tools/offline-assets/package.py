"""Assemble an optional Windows graphics-builder ZIP without game-derived data.

The supplied Python runtime must be a redistributable CPython+Tk+Pillow runtime.
SF3Tools, upscaler/models and CHD tools are obtained directly upstream at first run
and are deliberately excluded from this distribution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

REPO = Path(__file__).resolve().parents[2]
SCRIPTS = ('bootstrap.py', 'gui.py', 'worker.py', 'battle_textures.py', 'character_packs.py', 'index_packs.py',
           'archive_packs.py', 'select_packs.py', 'ExtractAssets.cs')


def external(path):
    path = Path(path).resolve()
    if any((p / '.git').exists() for p in (path, *path.parents)):
        raise ValueError('Runtime inputs and package outputs must stay outside source repositories')
    return path


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def package(destination, python, framework, make_zip=False):
    destination, python = external(destination), external(python)
    framework = Path(framework).resolve()
    if destination.exists():
        raise ValueError('Choose a new output folder; existing packages are preserved')
    if not (python / 'LICENSE.txt').is_file():
        raise ValueError('The Python runtime must retain its complete license')
    if not any((python / 'Lib/site-packages').glob('pillow-*.dist-info/licenses/LICENSE')):
        raise ValueError('Pillow distribution licenses are required')
    destination.mkdir(parents=True)
    runtime = destination / 'runtime'; runtime.mkdir()
    for name in ('python.exe', 'pythonw.exe', 'python3.dll', 'python312.dll', 'LICENSE.txt',
                 'vcruntime140.dll', 'vcruntime140_1.dll'):
        if (python / name).is_file():
            shutil.copy2(python / name, runtime / name)
    for name in ('DLLs', 'Lib', 'tcl'):
        shutil.copytree(python / name, runtime / name, ignore=shutil.ignore_patterns(
            '__pycache__', '*.pyc', 'site-packages', 'test', 'tests', 'idlelib', 'ensurepip', 'demos'))
    packages = runtime / 'Lib/site-packages'; packages.mkdir()
    for source in (python / 'Lib/site-packages').iterdir():
        if source.name == 'PIL' or source.name.lower().startswith(('pillow-', 'pillow.libs')):
            shutil.copytree(source, packages / source.name, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    # Python's shipped LICENSE.txt includes Tcl/Tk's license; retain its own local notice as well.
    app = destination / 'app'; app.mkdir()
    for name in SCRIPTS:
        shutil.copy2(Path(__file__).parent / name, app / name)
    for source, name in ((REPO / 'desktop/disc_input.py', 'disc_input.py'),
                         (REPO / 'tools/disc_profile.py', 'disc_profile.py'),
                         (REPO / 'desktop/ai_packs.py', 'ai_packs.py')):
        shutil.copy2(source, app / name)
    shutil.copytree(REPO / 'profiles', app / 'profiles', ignore=shutil.ignore_patterns('.*'))
    reader = app / 'vendor/saturnrecomp'; reader.mkdir(parents=True)
    shutil.copy2(framework / 'src/saturnrecomp/disc.py', reader / 'disc.py')
    (reader / '__init__.py').write_text('')
    shutil.copy2(REPO / 'LICENSES/saturn-recomp.txt', app / 'vendor/SATURN-RECOMP-LICENSE.txt')
    shutil.copy2(REPO / 'LICENSES/SF3Unity.txt', app / 'vendor/SF3UNITY-LICENSE.txt')
    shutil.copy2(REPO / 'LICENSE', destination / 'LICENSE.txt')
    shutil.copy2(REPO / 'THIRD_PARTY.md', destination / 'THIRD_PARTY.md')
    (destination / 'Start Offline Graphics Builder.cmd').write_text(
        '@echo off\ncd /d "%~dp0"\n"%~dp0runtime\\python.exe" -B "%~dp0app\\bootstrap.py"\n'
        'if errorlevel 1 pause\n', encoding='ascii')
    (destination / 'START HERE.txt').write_text(
        'SFIII Offline Graphics Builder — optional Windows x64 experiment\n\n'
        '1. Extract this WHOLE ZIP outside your source repository, then double-click Start Offline Graphics Builder.cmd.\n'
        '2. The first run downloads pinned tools directly from their authors and prepares the extractor automatically.\n'
        '   Allow several minutes and several GB of space. No compiler or Python installation is needed.\n'
        '3. Select your own supported Scenario 1/2/3 CUE or CHD files and GPU index.\n'
        '4. Start / Resume builds local AI world, CHR/CHP sprite and supported X8PC battle-model texture packs.\n'
        '   Keep all three styles\n'
        '   selected to attach packs to the game. Try Quick check before the full multi-hour GPU job.\n'
        '5. Once finished, use Attach packs to game and select the prepared game-data folder.\n'
        '   Restart the game and choose AI graphics Off, Animated, Illustrated or General.\n\n'
        'A Vulkan-capable GPU/driver is required. GPU indices are assigned by the upscaler; the first may be integrated graphics.\n'
        'Stop safely keeps completed cache. Closing the window leaves a running worker active. Codex can be closed.\n'
        'Output remains uncompressed for play; archive_packs.py creates verified lossless archives for your own backups.\n'
        'No API key, paid service or Codex tokens are used. No game data is uploaded. After one-time setup, CUE processing is offline.\n'
        'The optional CHD tool downloads directly upstream the first time a CHD is used.\n\n'
        'Experimental: unsupported formats/failed images remain original and are reported. Menus stay original;\n'
        'background planes, indexed spell effects and untextured battle polygons retain original graphics.\n'
        'Extracted textures are not proof of full gameplay coverage or complete attack/spell enhancements.\n'
        'Packs may require several GB per style; all-three-scenario output/cache can require tens of GB.\n'
        'Keep saves backed up. Packs are optional; detaching restores original graphics.\n\n'
        'Do not redistribute your game-derived output. SF3Tools has no redistribution license in the pinned upstream tree;\n'
        'this ZIP includes only our adapter source and fetches SF3Tools for local use. Do not repackage the prepared\n'
        'extractor, its upstream resources, setup-cache or setup-work. See THIRD_PARTY.md.\n', encoding='utf-8')
    inventory = {p.relative_to(destination).as_posix(): sha(p) for p in destination.rglob('*') if p.is_file()}
    forbidden = [name for name in inventory if Path(name).suffix.lower() in {'.bin', '.cue', '.chd', '.sav', '.state', '.rgba'}
                 or name.startswith(('output/', 'setup-cache/', 'setup-work/', 'app/extractor/', 'app/upscaler/', 'app/chdman/'))]
    if forbidden:
        raise ValueError('Unexpected game/upstream-local files in optional package: ' + str(forbidden))
    (destination / 'PACKAGE-FILES.json').write_text(json.dumps(inventory, indent=2) + '\n')
    probe = 'import sys,tkinter,PIL;print(sys.prefix);print(PIL.__version__);print(tkinter.Tcl().eval("info patchlevel"))'
    subprocess.run([str(runtime / 'python.exe'), '-B', '-c', probe], check=True)
    if make_zip:
        archive = destination.with_name(destination.name + '.zip')
        if archive.exists():
            raise ValueError('Archive already exists; previous package preserved')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
            for path in sorted(destination.rglob('*')):
                if path.is_file():
                    output.write(path, destination.name + '/' + path.relative_to(destination).as_posix())
        print(str(archive) + ' sha256=' + sha(archive), flush=True)
    print('Optional builder package: ' + str(destination), flush=True)
    return destination


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--python-runtime', type=Path, required=True)
    parser.add_argument('--framework', type=Path, default=REPO / '.local/saturn-recomp')
    parser.add_argument('--zip', action='store_true')
    args = parser.parse_args()
    package(args.out, args.python_runtime, args.framework, args.zip)


if __name__ == '__main__':
    main()
