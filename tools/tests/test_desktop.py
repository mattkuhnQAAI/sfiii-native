"""Synthetic preparation boundaries; no game files or system compiler required."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('desktop_prepare',Path(__file__).resolve().parents[2]/'desktop/prepare.py')
worker=importlib.util.module_from_spec(spec);spec.loader.exec_module(worker)

class DesktopTests(unittest.TestCase):
    def test_rejects_package_and_repository_data_directories(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);package=root/'app';package.mkdir()
            for path in [package,package/'data']:
                with self.assertRaises(ValueError):worker.external_work(path,package)
            repo=root/'source';repo.mkdir();(repo/'.git').mkdir()
            with self.assertRaises(ValueError):worker.external_work(repo/'data',package)
            self.assertEqual(worker.external_work(root/'data',package),(root/'data').resolve())

    def test_build_environment_isolates_system_compilers_and_runtime_overrides(self):
        fake={'SystemRoot':str(Path(tempfile.gettempdir())/'windows'),'PATH':'external-compiler','INCLUDE':'external-sdk',
              'LIB':'external-lib','CC':'system-compiler','CXX':'system-cxx','PYTHONPATH':'outside','SF3_WIDESCREEN':'1'}
        with patch.dict(os.environ,fake,clear=True):env=worker.build_environment(Path('/bundled'))
        for key in ['INCLUDE','LIB','CC','CXX','PYTHONPATH','SF3_WIDESCREEN']:self.assertNotIn(key,env)
        self.assertNotIn('external-compiler',env['PATH']);self.assertIn('compiler',env['PATH'])

    def test_failed_preparation_preserves_active_selection_and_saves(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);payload=root/'payload';payload.mkdir();work=root/'game';work.mkdir()
            (payload/'package.json').write_text(json.dumps({'build_id':'invented','version':'test'}))
            initial={'native-build.txt':b'build-previous','experimental-backup.bin':b'invented-internal',
                     'experimental-backup.bin.cartridge':b'invented-cartridge','desktop-ready.json':b'previous'}
            for name,data in initial.items():(work/name).write_bytes(data)
            (root/'fake.cue').touch()
            with patch.object(worker,'PAYLOAD',payload),patch.object(worker,'PACKAGE',root/'app'),patch.object(worker,'ensure_game_stopped'),patch.object(worker,'checked_disc',return_value=root/'fake.cue'),patch.object(worker,'disc_revision',return_value={'id':'synthetic','scenario':1}),patch.object(worker,'run',side_effect=RuntimeError('Invented failure')),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(RuntimeError):worker.prepare(root/'fake.cue',work)
            for name,data in initial.items():self.assertEqual((work/name).read_bytes(),data)

    def test_scenario_mismatch_never_replaces_existing_selection(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);payload=root/'payload';payload.mkdir();work=root/'game';work.mkdir()
            (payload/'package.json').write_text(json.dumps({'build_id':'synthetic'}))
            (root/'fake.cue').touch();(work/'native-build.txt').write_text('build-preserved')
            for selected in (1,2,3):
                for actual in (1,2,3):
                    if selected==actual:continue
                    with self.subTest(selected=selected,actual=actual),patch.object(worker,'PAYLOAD',payload),patch.object(worker,'PACKAGE',root/'app'),patch.object(worker,'ensure_game_stopped'),patch.object(worker,'checked_disc'),patch.object(worker,'disc_revision',return_value={'scenario':actual}),contextlib.redirect_stdout(io.StringIO()):
                        with self.assertRaisesRegex(ValueError,'Scenario '+str(actual)):worker.prepare(root/'fake.cue',work,selected)
            self.assertEqual((work/'native-build.txt').read_text(),'build-preserved')

    def test_another_revision_cannot_reuse_live_save_folder(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);payload=root/'payload';payload.mkdir();work=root/'game';work.mkdir()
            (payload/'package.json').write_text(json.dumps({'build_id':'synthetic'}))
            (root/'fake.cue').touch();(work/'game.json').write_text(json.dumps({'cue':'old.cue'}))
            (work/'experimental-backup.bin').write_bytes(b'preserved')
            with patch.object(worker,'PAYLOAD',payload),patch.object(worker,'PACKAGE',root/'app'),patch.object(worker,'ensure_game_stopped'),patch.object(worker,'checked_disc'),patch.object(worker,'disc_revision',side_effect=[{'scenario':2,'id':'new'},{'scenario':1,'id':'old'}]),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError,'another game revision'):worker.prepare(root/'fake.cue',work,2)
            self.assertEqual((work/'experimental-backup.bin').read_bytes(),b'preserved')

    def test_atomic_config_replaces_only_completed_json(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'game.json';path.write_text('{}')
            worker.atomic_json(path,{'cue':'invented.cue'})
            self.assertEqual(json.loads(path.read_text()),{'cue':'invented.cue'});self.assertFalse(path.with_suffix('.json.part').exists())

    def test_python_children_force_utf8_and_disable_private_bytecode_caches(self):
        with patch.object(worker.subprocess,'Popen') as popen:
            popen.return_value.stdout=iter([]);popen.return_value.wait.return_value=0
            worker.run(['python.exe','script.py'],{},io.StringIO(),'test')
            self.assertEqual(popen.call_args.args[0][1:4],['-X','utf8','-B'])

    @unittest.skipUnless(os.name=='nt','Windows file locking')
    def test_parallel_preparation_is_rejected_and_lock_releases(self):
        with tempfile.TemporaryDirectory() as d:
            work=Path(d)
            with worker.preparation_lock(work):
                with self.assertRaises(ValueError):
                    with worker.preparation_lock(work):pass
            with worker.preparation_lock(work):pass

if __name__=='__main__':unittest.main()
