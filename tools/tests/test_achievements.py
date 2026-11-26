import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('achievements', Path(__file__).resolve().parents[1]/'achievements.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AchievementTests(unittest.TestCase):
    def write(self, root, data):
        path = root/'achievements.json'
        path.write_text(json.dumps(data))
        return path

    def valid(self):
        return [dict(id=1, title='First', description='Do the first thing', points=5, trigger='0x10==1')]

    def test_load_valid_definition(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(Path(tmp), self.valid())
            self.assertEqual(module.load(path), self.valid())

    def test_condition_alias_and_default_points(self):
        with tempfile.TemporaryDirectory() as tmp:
            entry = dict(id='a', title='T', description='D', condition='1=1')
            got = module.load(self.write(Path(tmp), [entry]))[0]
            self.assertEqual(got['trigger'], '1=1')
            self.assertEqual(got['points'], 0)

    def test_rejects_non_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(Path(tmp), {'id': 1})
            with self.assertRaisesRegex(ValueError, 'JSON list'):
                module.load(path)

    def test_rejects_missing_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            for field in ('id', 'title', 'description', 'trigger'):
                entry = self.valid()[0].copy()
                del entry[field]
                path = self.write(Path(tmp), [entry])
                with self.subTest(field=field), self.assertRaises(ValueError):
                    module.load(path)

    def test_rejects_duplicate_and_invalid_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            dup = self.valid() + self.valid()
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                module.load(self.write(Path(tmp), dup))
            for bad in (-1, True, ''):
                entry = self.valid()[0].copy()
                entry['id'] = bad
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    module.load(self.write(Path(tmp), [entry]))

    def test_rejects_empty_and_bad_points(self):
        with tempfile.TemporaryDirectory() as tmp:
            for field in ('title', 'description', 'trigger'):
                entry = self.valid()[0].copy()
                entry[field] = ''
                with self.subTest(field=field), self.assertRaises(ValueError):
                    module.load(self.write(Path(tmp), [entry]))
            for bad in (-1, 1.5, '3'):
                entry = self.valid()[0].copy()
                entry['points'] = bad
                with self.subTest(points=bad), self.assertRaises(ValueError):
                    module.load(self.write(Path(tmp), [entry]))


if __name__ == '__main__':
    unittest.main()