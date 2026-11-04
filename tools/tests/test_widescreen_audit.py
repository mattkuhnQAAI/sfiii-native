"""Synthetic VDP1 command traversal and viewport boundary cases."""
import importlib.util
import io
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('wide_audit', Path(__file__).resolve().parents[1] / 'widescreen-audit.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def word(data, address, value):
    struct.pack_into('>H', data, address, value & 0xffff)


def quad(data, address, xs):
    word(data, address, 2)
    for i, x in enumerate(xs):
        word(data, address + 12 + i*4, x)
        word(data, address + 14 + i*4, i*4)


class WidescreenAuditTests(unittest.TestCase):
    def blank(self):
        return bytearray(audit.DUMP_SIZE)

    def test_local_coordinates_and_both_edges(self):
        data = self.blank()
        word(data, 0, 10)
        word(data, 12, -10)
        quad(data, 32, [0, 20, 20, 0])
        quad(data, 64, [329, 330, 330, 329])
        quad(data, 96, [330, 340, 340, 330])
        word(data, 128, 0x8000)
        result = audit.analyze(data)
        self.assertEqual(result['status'], 'end')
        self.assertEqual([q['horizontal'] for q in result['quads']],
                         ['crosses_left', 'crosses_right', 'outside_right'])
        self.assertEqual(result['quads'][0]['points'][0][0], -10)

    def test_call_return_and_skipped_command(self):
        data = self.blank()
        word(data, 0, 0x200a)  # call address 96 after applying local coordinates
        word(data, 2, 12)
        word(data, 32, 0x4002)  # skip a primitive
        word(data, 64, 0x8000)
        quad(data, 96, [1, 2, 2, 1])
        word(data, 96, 0x3002)  # draw and return
        result = audit.analyze(data)
        self.assertEqual(result['status'], 'end')
        self.assertEqual(result['command_counts'], {'10': 1, '2': 1})
        self.assertEqual(len(result['quads']), 1)

    def test_quad_spanning_both_edges(self):
        data = self.blank()
        quad(data, 0, [-1, 320, 320, -1])
        word(data, 32, 0x8000)
        self.assertEqual(audit.analyze(data)['quads'][0]['horizontal'], 'spans_both')

    def test_clip_state_is_recorded_without_claiming_visibility(self):
        data = self.blank()
        word(data, 0, 9)
        word(data, 20, 319)
        word(data, 22, 239)
        word(data, 32, 8)
        for offset, value in [(12, 4), (14, 6), (20, 300), (22, 220)]:
            word(data, 32 + offset, value)
        quad(data, 64, [-3, 10, 10, -3])
        word(data, 96, 0x8000)
        shape = audit.analyze(data)['quads'][0]
        self.assertEqual(shape['system_clip'], [319, 239])
        self.assertEqual(shape['user_clip'], [4, 6, 300, 220])
        self.assertEqual(shape['horizontal'], 'crosses_left')

    def test_cycle_guard_and_input_validation(self):
        data = self.blank()
        word(data, 0, 0x1000)  # jump to self
        self.assertEqual(audit.analyze(data)['status'], 'cycle_detected')
        self.assertEqual(audit.analyze(data, guard=1)['status'], 'guard_exhausted')
        with self.assertRaises(ValueError):
            audit.analyze(data[:-1])
        with self.assertRaises(ValueError):
            audit.analyze(data, width=0)

    def test_executed_trace_groups_draws_and_rejects_bad_control(self):
        data = self.blank()
        quad(data, 0, [-5, 10, 10, -5])
        header = 'field,draw,address,control,local_x,local_y,sys_x,sys_y,ux0,uy0,ux1,uy1,raw_words\n'
        command = data[:32].hex()
        line = f'100,7,32,2,0,0,319,239,0,0,319,239,{command}\n'
        result = audit.analyze_trace(io.StringIO(header + line + line.replace('100,7', '101,8')))
        self.assertEqual(result['status'], 'executed_trace')
        self.assertEqual(len(result['draws']), 2)
        self.assertEqual(result['draws'][0]['quad_edge_counts'], {'crosses_left': 1})
        with self.assertRaises(ValueError):
            audit.analyze_trace(io.StringIO(header + line.replace(',32,2,', ',32,3,')))
        with self.assertRaises(ValueError):
            audit.analyze_trace(io.StringIO(header))


if __name__ == '__main__':
    unittest.main()
