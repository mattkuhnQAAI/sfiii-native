"""Synthetic portable launcher/session boundaries; never read real game data."""
import contextlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'desktop'))
import prepare_posix as worker
import run_posix as sessions


def snapshot(path,identity):
    raw=b'SATSTATE'+struct.pack('<II',2,len(identity))+identity.encode()+b'synthetic parts'
    path.write_bytes(raw+struct.pack('<I',zlib.crc32(raw)))


class PortableTests(unittest.TestCase):
    def test_scenario3_selector_requires_its_own_ready_profile(self):
        import launcher_posix as launcher
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);data=root/'game';data.mkdir();payload=root/'payload';payload.mkdir()
            (payload/'package.json').write_text(json.dumps({'build_id':'synthetic-package'}))
            (data/'native-build.txt').write_text('build-synthetic')
            build=data/'build-synthetic';build.mkdir();(build/'saturn').write_bytes(b'invented executable')
            form=launcher.Launcher.__new__(launcher.Launcher)
            form.work=SimpleNamespace(get=lambda:str(data))
            for index,profile in enumerate(launcher.PROFILES+('scenario1-translation-v25_1',)):
                ready={'build_id':'synthetic-package','profile_id':profile,'executable_sha256':worker.common.sha(build/'saturn')}
                (data/'desktop-ready.json').write_text(json.dumps(ready))
                for selected in range(4):
                    form.active=selected
                    with self.subTest(prepared=index,selected=selected),patch.object(launcher,'PAYLOAD',payload):
                        self.assertEqual(form.ready(),selected==index)
            self.assertEqual(len(set(launcher.DATA_FOLDERS)),4)

    def test_bundle_boundary_includes_entire_mac_application(self):
        path=Path('/Applications/SFIII Native.app/Contents/Resources/payload')
        self.assertEqual(worker.package_root(path),Path('/Applications/SFIII Native.app'))

    def test_configure_uses_bundled_tools_and_relocatable_libraries(self):
        with patch.object(worker.sys,'platform','darwin'):
            args=list(map(str,worker.configure_args(Path('/generated'),Path('/build'),Path('/payload'))))
        self.assertIn('-DCMAKE_CXX_COMPILER='+str(Path('/payload/compiler/bin/c++')),args)
        self.assertIn('-DCMAKE_AR='+str(Path('/payload/compiler/bin/ar')),args)
        self.assertIn('-DCMAKE_RANLIB='+str(Path('/payload/compiler/bin/ranlib')),args)
        self.assertIn('-DCMAKE_INSTALL_RPATH=@executable_path/lib',args)
        self.assertNotIn('-DCMAKE_EXE_LINKER_FLAGS=-static',args)
        with patch.object(worker.sys,'platform','linux'):
            self.assertIn('-DCMAKE_INSTALL_RPATH=$ORIGIN/lib',worker.configure_args(Path('/g'),Path('/b'),Path('/payload')))

    def test_environment_removes_compiler_injection_and_runtime_overrides(self):
        with patch.dict(os.environ,{'PATH':'untrusted','SDKROOT':'untrusted','CPATH':'untrusted','SF3_WIDESCREEN':'1','LD_PRELOAD':'untrusted'},clear=True),patch.object(worker.Path,'home',return_value=Path('/synthetic/home')):
            env=worker.environment(Path('/payload'))
        for key in ['SDKROOT','CPATH','SF3_WIDESCREEN','LD_PRELOAD']:self.assertNotIn(key,env)
        self.assertNotIn('untrusted',env['PATH'])

    @unittest.skipIf(os.name=='nt','POSIX native file lock is tested on Linux/macOS runners')
    def test_game_setup_and_import_cannot_share_live_folder(self):
        with tempfile.TemporaryDirectory() as d:
            with worker.lock(Path(d)):
                with self.assertRaises(ValueError):
                    with worker.lock(Path(d)):pass
            with worker.lock(Path(d)):pass

    def test_state_identity_and_corruption_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            file=Path(d)/'state.satstate'
            snapshot(file,'synthetic-build:synthetic-disc')
            sessions.check_state(file,'synthetic-build:synthetic-disc')
            with self.assertRaisesRegex(ValueError,'another native build'):sessions.check_state(file,'wrong')
            raw=bytearray(file.read_bytes());raw[-5]^=1;file.write_bytes(raw)
            with self.assertRaisesRegex(ValueError,'Damaged'):sessions.check_state(file,'synthetic-build:synthetic-disc')

    def test_paths_cannot_escape_build_or_state_roots(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            (root/'native-build.txt').write_text('build-../../elsewhere')
            with self.assertRaises(ValueError):sessions.selected_build(root)
            with self.assertRaises(ValueError):sessions.within(root.parent,root)

    def test_native_load_preserves_exact_preload_backups_and_has_no_replay_flags(self):
        with tempfile.TemporaryDirectory() as d:
            work=Path(d)/'data';work.mkdir()
            (work/'native-build.txt').write_text('build-synthetic')
            (work/'build-synthetic').mkdir()
            cue=Path(d)/'synthetic.cue';cue.write_text('Invented test disc')
            (work/'game.json').write_text(json.dumps({'cue':str(cue)}))
            original={'experimental-backup.bin':b'original-internal','experimental-backup.bin.cartridge':b'original-cartridge'}
            for name,data in original.items():(work/name).write_bytes(data)
            state=work/'native-states/synthetic/latest.satstate';state.parent.mkdir(parents=True)
            snapshot(state,'build:disc')
            commands=[]
            def run(args,**kwargs):commands.append(args);return SimpleNamespace(returncode=0)
            with patch.object(sessions,'lock',return_value=contextlib.nullcontext()),patch.object(sessions,'package_root',return_value=Path(d)/'app'),patch.object(sessions,'identities',return_value=('build','disc')),patch.object(sessions.subprocess,'run',side_effect=run):
                sessions.play(work,state)
            self.assertEqual(len(commands),1)
            self.assertIn('--state-in',commands[0]);self.assertNotIn('--resume',commands[0]);self.assertNotIn('--input',commands[0])
            session=next((work/'sessions').iterdir())
            for name,data in original.items():
                self.assertEqual((work/name).read_bytes(),data)
                self.assertEqual((session/('before-native-state-load-'+name)).read_bytes(),data)
            self.assertEqual((session/'loaded.satstate').read_bytes(),state.read_bytes())
            self.assertFalse(json.loads((session/'session.json').read_text())['live_replay_progress_published'])

    def test_legacy_replay_without_cartridge_or_settings_does_not_use_live_values(self):
        with tempfile.TemporaryDirectory() as d:
            work=Path(d)/'data';work.mkdir()
            (work/'native-build.txt').write_text('build-synthetic')
            (work/'build-synthetic').mkdir()
            cue=Path(d)/'synthetic.cue';cue.write_text('Invented test disc')
            (work/'game.json').write_text(json.dumps({'cue':str(cue)}))
            (work/'experimental-backup.bin').write_bytes(b'live-internal')
            (work/'experimental-backup.bin.cartridge').write_bytes(b'live-cartridge')
            (work/'settings.ini').write_text('live-settings')
            replay=work/'replay-checkpoints/123-5'
            (replay/'seed').mkdir(parents=True)
            for name,data in {'backup.bin':b'old-internal','build-id.txt':b'build','disc-id.txt':b'disc',
                              'context.json':json.dumps({'clock':'2000-01-02T03:04:05'}).encode()}.items():
                (replay/'seed'/name).write_bytes(data)
            (replay/'input.txt').write_text('1:START,2:,')
            games=[]
            def run(args,**kwargs):
                if args[1]=='verify':return SimpleNamespace(returncode=0,stdout='5\n')
                games.append(args)
                out=Path(args[args.index('--out')+1])
                if len(games)==1:(out/'reload-request.txt').write_text(str(replay))
                else:
                    save=Path(args[args.index('--save')+1])
                    self.assertEqual(save.read_bytes(),b'old-internal')
                    self.assertEqual(Path(str(save)+'.cartridge').read_bytes(),b'')
                    self.assertFalse(Path(args[args.index('--settings')+1]).exists())
                    self.assertEqual(args[args.index('--clock')+1],'2000-01-02T03:04:05')
                return SimpleNamespace(returncode=0)
            with patch.object(sessions,'lock',return_value=contextlib.nullcontext()),patch.object(sessions,'package_root',return_value=Path(d)/'app'),patch.object(sessions,'identities',return_value=('build','disc')),patch.object(sessions.subprocess,'run',side_effect=run):sessions.play(work)
            self.assertEqual(len(games),2)
            self.assertEqual((work/'experimental-backup.bin').read_bytes(),b'live-internal')
            self.assertEqual((work/'experimental-backup.bin.cartridge').read_bytes(),b'live-cartridge')


if __name__=='__main__':unittest.main()
