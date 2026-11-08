"""volatile_literals() and a pool variable stored in the delay slot of the branch after its `mova`
(`mova SLOT,r0; rts; mov.w r4,@r0`). Synthetic 64-byte images only, no game data."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'.local/saturn-recomp/src'))
from saturnrecomp import sh2
from saturnrecomp.recomp import discover
from saturnrecomp.recomp.__main__ import volatile_literals

BASE = 0x06004000


def program(words, seeds):
    data = bytearray(0x40)
    for i, w in enumerate(words):
        data[i * 2:i * 2 + 2] = w.to_bytes(2, 'big')
    return discover.Program(sh2.Image(bytes(data), BASE), [BASE + s for s in seeds])


# +00 reader: mov.w @(SLOT),r0; rts; nop       +06 setter: mova SLOT,r0; <tail>       +10 SLOT
def image(tail):
    return [0x9006, 0x000B, 0x0009, 0xC702] + tail + [0x0000] * (4 - len(tail)) + [0xFFE0]


SLOT = BASE + 0x10


class VolatileLiterals(unittest.TestCase):
    def test_store_in_rts_delay_slot(self):            # mova; rts; mov.w r4,@r0  (the case found)
        self.assertIn(SLOT, volatile_literals(program(image([0x000B, 0x2041]), [0, 6])))

    def test_store_in_bra_delay_slot(self):            # mova; bra +0; mov.w r4,@r0
        self.assertIn(SLOT, volatile_literals(program(image([0xAFFB, 0x2041]), [0, 6])))

    def test_straight_line_store_already_found(self):  # mova; mov.w r4,@r0; rts; nop
        self.assertIn(SLOT, volatile_literals(program(image([0x2041, 0x000B, 0x0009]), [0, 6])))

    def test_no_store_stays_foldable(self):            # mova; rts; nop: r0 returned, nothing stored
        self.assertNotIn(SLOT, volatile_literals(program(image([0x000B, 0x0009]), [0, 6])))

    def test_slot_after_r0_is_rewritten_is_not_a_store(self):  # mova; mov #0,r0; rts; mov.w r4,@r0
        self.assertNotIn(SLOT, volatile_literals(program(image([0xE000, 0x000B, 0x2041]), [0, 6])))


if __name__ == '__main__':
    unittest.main()
