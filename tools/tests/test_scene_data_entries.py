"""Synthetic checks for scene data-table entries (tools/scene_data_entries.py); no game data."""
from pathlib import Path
import sys
import unittest

tools = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(tools))
sys.path.insert(0, str(tools.parent / ".local/saturn-recomp/src"))
try:
    from saturnrecomp.sh2 import Image
    from saturnrecomp.recomp.discover import Program
except ImportError:
    Image = Program = None
from scene_data_entries import data_table_entries, is_scene

BASE = 0x6000000


def image():
    words = [0x0000] * 0x300
    def put(offset, *ws):
        for i, w in enumerate(ws):
            words[offset // 2 + i] = w
    def ptr(offset, value):
        put(offset, value >> 16, value & 0xFFFF)
    put(0x00, 0xD001, 0x000B, 0x0009)   # mov.l @(8),r0; rts; nop
    ptr(0x08, BASE + 0xC0)              # literal of known code, not a table word
    put(0x68, 0x6141, 0x6141)           # straight-line code ahead: 0x6C is not a boundary
    put(0x6C, 0x740A, 0x000B, 0x2411)   # short callback: add #10,r4; rts; mov.w r1,@r4
    put(0xA0, 0xA000, 0x000B)           # bra with a branch in its delay slot
    put(0xC0, 0x000B, 0x0009)
    put(0x200, *[0x0009] * 0x100)       # straight-line code with no terminator
    table = [BASE + 0x6C, BASE + 0x00, BASE + 0x02, BASE + 0x08, BASE + 0x80, BASE + 0xA0,
             BASE + 0x200, BASE + 0x71, 0x07000000, BASE + 0x6C]
    for i, value in enumerate(table):   # script-style records: (count, pointer) pairs
        ptr(0x100 + 8 * i, 0x26)
        ptr(0x104 + 8 * i, value)
    return Image(b"".join(w.to_bytes(2, "big") for w in words), BASE)


@unittest.skipUnless(Program, "framework source unavailable; run dependency setup")
class SceneDataEntries(unittest.TestCase):
    def setUp(self):
        self.img = image()
        self.accepted, rows = data_table_entries(self.img, Program(self.img, [BASE]))
        self.rows = {t - BASE: (v, why, refs) for t, v, why, refs in rows}

    def test_callback_referenced_only_from_a_table_is_accepted(self):
        self.assertEqual(self.accepted, {BASE + 0x6C})
        verdict, reason, refs = self.rows[0x6C]
        self.assertEqual((verdict, reason), ("accepted", "plausible code"))
        self.assertEqual(refs, [BASE + 0x104, BASE + 0x14C])
        program = Program(self.img, [BASE] + sorted(self.accepted))
        self.assertIn(BASE + 0x6C, program.funcs)
        self.assertIsNone(program.funcs[BASE + 0x6C].bad)

    def test_framework_discovery_alone_misses_the_short_callback(self):
        # The gap the rule closes: a short callback, not at a boundary, not loaded as a
        # literal and not in a table of pointers is not taken by pointer seeding.
        self.assertNotIn(BASE + 0x6C, Program(self.img, [BASE]).funcs)

    def test_known_entry_is_logged_not_added(self):
        self.assertEqual(self.rows[0x00][:2], ("known", "already an entry"))

    def test_rejections_are_logged_with_a_reason(self):
        self.assertEqual(self.rows[0x02][:2], ("rejected", "inside known code"))
        self.assertEqual(self.rows[0x08][:2], ("rejected", "a literal-pool slot"))
        self.assertEqual(self.rows[0x80][:2], ("rejected", "illegal 0000 at %08X" % (BASE + 0x80)))
        self.assertEqual(self.rows[0xA0][:2], ("rejected", "illegal delay slot 000B at %08X" % (BASE + 0xA2)))
        self.assertEqual(self.rows[0x200][:2], ("rejected", "no unconditional branch within 256 instructions"))

    def test_odd_out_of_range_and_known_literals_are_not_candidates(self):
        self.assertNotIn(0x71, self.rows)
        self.assertNotIn(0x07000000 - BASE, self.rows)
        self.assertNotIn(0xC0, self.rows)
        self.assertEqual(set(self.rows), {0x00, 0x02, 0x08, 0x6C, 0x80, 0xA0, 0x200})


@unittest.skipUnless(Program, "framework source unavailable; run dependency setup")
class RegressionSeeds(unittest.TestCase):
    @staticmethod
    def scene(base):
        words = [0x0000] * 0x80
        words[0:2] = [0x000B, 0x0009]
        words[0x68 // 2:0x6C // 2] = [0x6141, 0x6141]
        words[0x6C // 2:0x72 // 2] = [0x740A, 0x000B, 0x2411]   # no table points here
        return Image(b"".join(w.to_bytes(2, "big") for w in words), base)

    def test_guard_seed_is_validated_and_added_for_jasn01_and_futtin(self):
        img = self.scene(0x0605E000)
        for name in ("X1JASN01", "X1FUTTIN"):
            accepted, rows = data_table_entries(img, Program(img, [img.base]), name)
            self.assertEqual(accepted, {0x0605E06C}, name)
            self.assertEqual(rows, [(0x0605E06C, "accepted", "regression seed, plausible code", [])])
        self.assertEqual(data_table_entries(img, Program(img, [img.base]), "X1OTHER")[0], set())

    def test_guard_seed_outside_the_image_is_skipped(self):
        img = self.scene(0x0605F000)    # Scenario 1 scene base
        self.assertEqual(data_table_entries(img, Program(img, [img.base]), "X1JASN01"), (set(), []))

    def test_guard_seed_that_is_not_code_is_rejected(self):
        img = Image(bytes(0x100), 0x0605E000)
        accepted, rows = data_table_entries(img, Program(img, [img.base]), "X1JASN01")
        self.assertEqual(accepted, set())
        self.assertEqual(rows[-1][:2], (0x0605E06C, "rejected"))


class SceneNames(unittest.TestCase):
    def test_only_x1_scene_modules_use_the_rule(self):
        for name in ("X1JASN01", "X1FUTTIN", "X1SHOP_T", "X12BAL_1"):
            self.assertTrue(is_scene(name), name)
        for name in ("X005", "X3ITEM", "BOOT", "X1jasn01", "X1JASN01.BIN"):
            self.assertFalse(is_scene(name), name)


if __name__ == "__main__":
    unittest.main()
