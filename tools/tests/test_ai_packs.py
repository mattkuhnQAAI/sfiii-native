"""Revision-bound optional artwork selection; all fixtures are invented and external."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('sf3_ai_packs', Path(__file__).resolve().parents[2] / 'desktop/ai_packs.py')
packs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packs)


class ArtworkSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.game = self.root / 'game'
        self.game.mkdir()
        self.profiles = self.root / 'profiles'
        self.profiles.mkdir()
        self.profile = dict(id='synthetic-test-profile', scenario=2, content_sha256='a'*64)
        self.write(self.profiles / 'synthetic-revision.json', self.profile)
        self.write(self.game / 'game.json', dict(profile_id=self.profile['id'], cue='never-read.cue'))
        self.rows = []
        for model in packs.MODEL_ENV:
            directory = self.root / ('pack-' + model)
            directory.mkdir()
            record = dict(profile=self.profile['id'], revision='a'*64, scenario=2,
                          model=model, material_count=1, path=str(directory), missing=[])
            self.write(directory / 'manifest.json', record)
            header = bytearray(64)
            header[:8] = b'SF3HDIX1'
            struct.pack_into('<IIQ', header, 8, 1, 64, 64)
            struct.pack_into('<I', header, 40, 1)
            (directory / 'materials.sf3idx').write_bytes(header)
            self.rows.append(record)
        self.report = self.root / 'packs-report.json'
        self.write(self.report, self.rows)

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding='utf-8')

    def install(self):
        return packs.install_selection(self.report, self.game, self.profiles)

    def test_absent_is_optional_attach_env_and_detach_preserve_selection(self):
        self.assertEqual(packs.validate_selection(self.game, self.profiles), {})
        record = self.install()
        expected = {packs.MODEL_ENV[model]: str(self.root / ('pack-' + model)) for model in packs.MODEL_ENV}
        self.assertEqual(packs.validate_selection(self.game, self.profiles), expected)
        self.install()
        self.assertEqual(len(list(self.game.glob('ai-packs.previous-*.json'))), 1)
        backup = packs.detach_selection(self.game)
        self.assertEqual(json.loads(Path(backup).read_text()), record)
        self.assertEqual(packs.validate_selection(self.game, self.profiles), {})
        self.assertEqual(len(list(self.game.glob('ai-packs.previous-*.json'))), 2)
        self.assertIsNone(packs.detach_selection(self.game))

    def test_profile_and_revision_are_pinned_not_trusted_report_labels(self):
        self.rows[0]['revision'] = 'b'*64
        self.write(self.report, self.rows)
        with self.assertRaisesRegex(ValueError, 'another disc revision'):
            self.install()
        self.rows[0]['revision'] = 'a'*64
        self.write(self.report, self.rows)
        marker = Path(self.rows[0]['path']) / 'manifest.json'
        changed = json.loads(marker.read_text())
        changed['revision'] = 'b'*64
        self.write(marker, changed)
        with self.assertRaisesRegex(ValueError, 'different disc revision'):
            self.install()
        self.assertFalse((self.game / packs.SELECTION).exists())

    def test_missing_manifest_incomplete_styles_or_wrong_count_rejected(self):
        self.write(self.report, self.rows[:1])
        with self.assertRaisesRegex(ValueError, 'three styles'):
            self.install()
        self.write(self.report, self.rows)
        self.rows[0]['material_count'] = 2
        self.write(self.report, self.rows)
        with self.assertRaisesRegex(ValueError, 'count'):
            self.install()
        self.rows[0]['material_count'] = 1
        self.write(self.report, self.rows)
        marker = Path(self.rows[0]['path']) / 'manifest.json'
        marker.unlink()
        with self.assertRaises(OSError):
            self.install()

    def test_changed_manifest_rejects_launch_without_replacing_progress(self):
        self.install()
        save = self.game / 'experimental-backup.bin'
        save.write_bytes(b'invented current progress')
        marker = Path(self.rows[1]['path']) / 'manifest.json'
        marker.write_text(marker.read_text() + '\n')
        with self.assertRaisesRegex(ValueError, 'changed after attachment'):
            packs.validate_selection(self.game, self.profiles)
        self.assertEqual(save.read_bytes(), b'invented current progress')

    def test_source_git_relative_and_parent_traversal_rejected(self):
        for path in (Path('relative/path'), self.root / 'child' / '..' / 'game', packs.SOURCE_ROOT):
            with self.subTest(path=str(path)), self.assertRaises(ValueError):
                packs.external(path)
        (self.root / '.git').mkdir()
        with self.assertRaisesRegex(ValueError, 'outside a source repository'):
            self.install()

    def test_selection_cannot_switch_disc_or_partial_styles(self):
        record = self.install()
        record['profile'] = 'another-profile'
        self.write(self.game / packs.SELECTION, record)
        with self.assertRaisesRegex(ValueError, 'another prepared disc revision'):
            packs.validate_selection(self.game, self.profiles)
        record['profile'] = self.profile['id']
        del record['models']['anime']
        self.write(self.game / packs.SELECTION, record)
        with self.assertRaisesRegex(ValueError, 'Animated, Illustrated and General'):
            packs.validate_selection(self.game, self.profiles)

    def test_legacy_preparation_without_verified_profile_must_be_reprepared(self):
        self.write(self.game / 'game.json', dict(cue='never-read.cue'))
        with self.assertRaisesRegex(ValueError, 'Prepare this game again'):
            self.install()

    def test_builder_profile_filename_resolves_to_same_pinned_identity(self):
        for row in self.rows:
            row['profile'] = 'synthetic-revision.json'
            self.write(Path(row['path']) / 'manifest.json', row)
        self.write(self.report, self.rows)
        record = self.install()
        self.assertEqual(record['profile'], self.profile['id'])
        self.assertEqual(len(packs.validate_selection(self.game, self.profiles)), 3)

    def test_missing_or_corrupt_index_retains_native_directory_fallback(self):
        (Path(self.rows[0]['path']) / 'materials.sf3idx').unlink()
        (Path(self.rows[1]['path']) / 'materials.sf3idx').write_bytes(b'corrupt synthetic index')
        self.install()
        self.assertEqual(len(packs.validate_selection(self.game, self.profiles)), 3)

    def test_copied_preparation_helper_can_attach_and_resolve_its_own_game_folder(self):
        helper = self.game / 'ai-packs-helper.py'
        shutil.copy2(packs.__file__, helper)
        args = ['--game-dir', str(self.game), '--profiles', str(self.profiles)]
        attached = subprocess.run([sys.executable, '-B', str(helper), 'attach', '--report', str(self.report), *args],
                                  check=True, text=True, capture_output=True)
        self.assertEqual(json.loads(attached.stdout)['profile'], self.profile['id'])
        result = subprocess.run([sys.executable, '-B', str(helper), 'env', *args],
                                check=True, text=True, capture_output=True)
        self.assertEqual(set(json.loads(result.stdout)), set(packs.MODEL_ENV.values()))

    @unittest.skipUnless(os.name == 'nt', 'Windows launcher environment integration unavailable on this platform')
    def test_windows_launcher_propagates_valid_packs_clears_inherited_packs_and_rejects_wrong_revision(self):
        windows = Path(os.environ.get('SystemRoot', 'C:/Windows'))
        compiler = windows / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
        shell = windows / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        if not compiler.is_file() or not shell.is_file():
            self.skipTest('Windows .NET compiler or PowerShell unavailable')
        source = self.root / 'InventedHost.cs'
        source.write_text('''using System; using System.IO; using System.Collections.Generic;
using System.Web.Script.Serialization;
class InventedHost { static void Main(string[] args) {
var values=new Dictionary<string,string>();
foreach(var name in new[]{"SF3_AI_VIDEO_DIR","SF3_AI_ASSET_DIR","SF3_AI_NATURAL_DIR"})
values[name]=Environment.GetEnvironmentVariable(name);
File.WriteAllText(Environment.GetEnvironmentVariable("SF3_TEST_RECEIPT"),
new JavaScriptSerializer().Serialize(new {arguments=args,artwork=values})); }}''', encoding='utf-8')
        build = self.game / 'build-synthetic'
        build.mkdir()
        exe = build / 'saturn.exe'
        subprocess.run([str(compiler), '/nologo', '/target:exe', '/reference:System.Web.Extensions.dll',
                        '/out:' + str(exe), str(source)], check=True, capture_output=True)
        shutil.copy2(exe, build / 'replay_checkpoint.exe')
        shutil.copy2(packs.__file__, self.game / 'ai-packs-helper.py')
        shutil.copytree(self.profiles, self.game / 'ai-profiles')
        (self.game / 'save-import-python.txt').write_text(sys.executable, encoding='utf-8')
        (self.game / 'native-build.txt').write_text(build.name, encoding='utf-8')
        (self.game / 'invented.bin').write_bytes(bytes(range(256)) * 10)
        cue = self.game / 'invented.cue'
        cue.write_text('FILE "invented.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n')
        self.write(self.game / 'game.json', dict(profile_id=self.profile['id'], cue=str(cue)))
        receipt = self.root / 'host-receipt.json'
        environment = dict(os.environ, SF3_TEST_RECEIPT=str(receipt),
                           PSModulePath=str(shell.parent / 'Modules'),
                           **{name: 'inherited-unrelated-pack' for name in packs.MODEL_ENV.values()})
        launcher = Path(__file__).resolve().parents[1] / 'run-native.ps1'
        command = [str(shell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(launcher),
                   '-Work', str(self.game), '-Headless', '-VBlanks', '1']
        def launch():
            return subprocess.run(command, env=environment, capture_output=True, text=True)
        result = launch()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(all(value in (None, '') for value in json.loads(receipt.read_text())['artwork'].values()))
        self.install()
        result = launch()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(receipt.read_text())['artwork'],
                         packs.validate_selection(self.game, self.profiles))
        receipt.unlink()
        selected = json.loads((self.game / packs.SELECTION).read_text())
        selected['revision'] = 'b'*64
        self.write(self.game / packs.SELECTION, selected)
        result = launch()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(receipt.exists(), 'Invalid artwork must not reach the native executable.')


if __name__ == '__main__':
    unittest.main()
