import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('site_review', Path(__file__).parents[1]/'widescreen-site-review.py')
review = importlib.util.module_from_spec(spec)
spec.loader.exec_module(review)


class SiteReviewTests(unittest.TestCase):
    signature = bytes(range(32))

    def test_relocation_and_last_complete_instruction(self):
        self.assertEqual(review.find_sites(b'xx'+self.signature,self.signature,0x6004000),[0x6004002])

    def test_ambiguous_and_unaligned(self):
        s = self.signature
        self.assertEqual(review.find_sites(s+s,s,0),[0,32])
        self.assertEqual(review.find_sites(b'x'+s,s,0),[])

    def test_changed_or_truncated(self):
        s = self.signature
        self.assertEqual(review.find_sites(s[:-1],s,0),[])
        self.assertEqual(review.find_sites(s[:-1]+b'x',s,0),[])

    def test_invalid_extents(self):
        for base in (-1,1,2**32-2,True):
            with self.assertRaises(ValueError): review.find_sites(self.signature,self.signature,base)
        with self.assertRaises(ValueError): review.find_sites(self.signature,b'xx',0)

    def test_verified_inputs_and_duplicate_labels(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'synthetic').write_bytes(self.signature)
            identity={'file':'synthetic','sha256':hashlib.sha256(self.signature).hexdigest()}
            manifest={'reference':dict(identity,offset=0,length=32),
                      'candidates':[dict(identity,edition='invented',base=0)]}
            self.assertEqual(review.review(manifest,root)['editions'][0]['status'],'unique-byte-match')
            manifest['candidates'] *= 2
            with self.assertRaises(ValueError): review.review(manifest,root)
            (root/'synthetic').write_bytes(b'x'*32)
            with self.assertRaises(ValueError): review.review(manifest,root)


if __name__ == '__main__': unittest.main()
