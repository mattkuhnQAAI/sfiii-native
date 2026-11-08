import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ROOT/'profiles'
# add-on profile -> the V25.1 profile its seeds were copied from
ADDONS = {'scenario1-v25_1-addons': 'scenario1-v25_1',
          'scenario2-v25_1-addons-v1_003': 'scenario2-v25_1-v1_003',
          'scenario2-v25_1-addons-v2_000': 'scenario2-v25_1-v2_000',
          'scenario3-v25_1-addons-redump': 'scenario3-v25_1-redump'}
DESCRIPTIVE = {'id', 'title', 'version', 'content_sha256', 'support'}


def load(name):
    return json.loads((PROFILES/(name+'.json')).read_text(encoding='utf-8-sig'))


class AddonProfiles(unittest.TestCase):
    def test_addon_profiles_keep_their_base_generation_settings(self):
        for addon, base in ADDONS.items():
            with self.subTest(addon=addon):
                a, b = load(addon), load(base)
                self.assertEqual(set(a), set(b))
                self.assertEqual({k: v for k, v in a.items() if k not in DESCRIPTIVE},
                                 {k: v for k, v in b.items() if k not in DESCRIPTIVE})
                self.assertNotEqual(a['content_sha256'], b['content_sha256'])
                self.assertNotEqual(a['id'], b['id'])

    def test_every_registered_disc_identity_is_unique(self):
        ids = [json.loads(p.read_text(encoding='utf-8-sig')).get('content_sha256') for p in sorted(PROFILES.glob('*.json'))]
        ids = [i for i in ids if i]
        self.assertEqual(len(ids), len(set(ids)))


if __name__ == '__main__':
    unittest.main()
