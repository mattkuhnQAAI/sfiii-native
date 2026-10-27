"""Synthetic SH-2 continuation cases; no game data or generated game code."""
import re
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

FRAMEWORK = Path(__file__).resolve().parents[2]/'.local/saturn-recomp/src'
sys.path.insert(0, str(FRAMEWORK))
from saturnrecomp.recomp.__main__ import Module
from saturnrecomp.recomp.emit import Body
from saturnrecomp.sh2 import Image


class NativeContinuations(unittest.TestCase):
    # Caller saves PR, calls an increment routine, restores PR and returns.
    code = bytes.fromhex('e0054f22b00300094f26000b00097001000b0009')

    def generate(self, root, enabled):
        source=root/'synthetic.bin';source.write_bytes(self.code)
        m=Module('SYNTH',str(source),0x06001000,native_states=enabled)
        m.generate(str(root),8000,True,lambda *_:None)
        text='\n'.join((root/name).read_text() for name in m.files)
        return m,text

    def test_call_returns_are_native_entries_and_delay_slots_are_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            m,text=self.generate(Path(tmp),True)
            self.assertIn(0x06001008,m.entries)
            self.assertIn('void f_06001008(SH2Context& c)',text)
            self.assertNotIn(0x06001006,m.entries)
            self.assertIn('SH2_STATE_POINT(c, 0x0600100Eu)',text)
            self.assertEqual(set(re.findall(r'goto (L_[0-9A-F]+)',text))-set(re.findall(r'(L_[0-9A-F]+):',text)),set())

    def test_disabled_generation_keeps_original_call_structure(self):
        with tempfile.TemporaryDirectory() as tmp:
            m,text=self.generate(Path(tmp),False)
            self.assertNotIn(0x06001008,m.entries)
            self.assertNotIn('SH2_STATE_POINT',text)

    def test_empty_discovery_stub_has_no_undefined_resume_label(self):
        fn=SimpleNamespace(entry=0x06001000,code=set())
        prog=SimpleNamespace(img=Image(b'',fn.entry))
        body=Body(prog,fn,{fn.entry},resume_points={fn.entry})
        self.assertNotIn('goto L_', '\n'.join(body.emit()))

    def test_backward_branch_target_can_resume_without_repeating_delay_slot(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.code=bytes.fromhex('e0027001affe7101000b0009')
            m,text=self.generate(Path(tmp),True)
            self.assertIn(0x06001004,m.entries)
            self.assertNotIn(0x06001006,m.entries)
            self.assertIn('SH2_STATE_POINT(c, 0x06001004u)',text)

    def test_shared_loop_still_offers_a_safe_point_in_each_body(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.code=bytes.fromhex('e0027001affe7101000b0009')
            source=Path(tmp)/'synthetic.bin';source.write_bytes(self.code)
            m=Module('SYNTH',str(source),0x06001000,native_states=True)
            fn=m.prog.funcs[0x06001000]
            # Another body may own the exported loop continuation. This body
            # must still park there when the slave waits inside the shared loop.
            body=Body(m.prog,fn,set(m.entries),resume_points={fn.entry})
            self.assertIn('SH2_STATE_POINT(c, 0x06001004u)', '\n'.join(body.emit()))

    def test_hand_written_mova_pr_continuation_is_exported(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.code=bytes.fromhex('c702402aa002000900090009000b0009')
            m,text=self.generate(Path(tmp),True)
            self.assertIn(0x0600100C,m.entries)
            self.assertIn('void f_0600100C(SH2Context& c)',text)

    def test_state_points_come_before_their_block_charge(self):
        # A loaded state resumes at the label and runs what follows it, so a
        # point after the block's SH2_CYC would charge that block twice and the
        # loaded run would part from the run that saved it.
        with tempfile.TemporaryDirectory() as tmp:
            for code in (self.code, bytes.fromhex('e0027001affe7101000b0009')):
                self.code=code
                m,text=self.generate(Path(tmp),True)
                lines=[l.strip() for l in text.splitlines()]
                points=[i for i,l in enumerate(lines) if l.startswith('SH2_STATE_POINT(')]
                self.assertTrue(points)
                for i in points:
                    self.assertFalse(lines[i-1].startswith('SH2_CYC('),lines[i-2:i+2])
                    if lines[i-1].startswith('L_'):self.assertTrue(lines[i+1].startswith('SH2_CYC('),lines[i-1:i+2])


if __name__=='__main__':unittest.main()
