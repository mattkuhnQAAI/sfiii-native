"""Synthetic discovery cases, including incomplete and misleading byte streams."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from module_entries import unrolled_copy_entries, arithmetic_shift_entries, registered_store_callback_entries, vertex_producer_hooks, missing_vertex_hooks
from unittest.mock import patch

class VertexProducerHooks(unittest.TestCase):
    def test_all_emitted_copies_require_the_instruction_observer(self):
        observed='c.r[5] >>= 16; sh2_hook(c, 0x06000010u); // 06000010 shlr16 r5\n'
        bypassed='c.r[5] >>= 16; // 06000010 shlr16 r5\n'
        self.assertEqual(missing_vertex_hooks(observed,{0x06000010}),[])
        self.assertEqual(missing_vertex_hooks('',{0x06000010}),[0x06000010])
        self.assertEqual(missing_vertex_hooks(observed+bypassed,{0x06000010}),[0x06000010])
    def test_reviewed_profiles_get_all_three_entries_and_queue(self):
        for size,crc,formatter,queue in [(117672,0xEBEEDDFA,0x06017144,0x06016700),(123940,0xCC57FA60,0x06016A38,0x06015FEC),(123872,0x40B12530,0x060169F0,0x06015F9C),(124272,0xA9FE4141,0x06016A70,0x06015FE4)]:
            # Invented zero-filled data; real local BOOT identity/execution is
            # checked separately by check-vertex-producers.py.
            with patch('zlib.crc32',return_value=crc):
                self.assertEqual(vertex_producer_hooks(bytes(size),0x06004000),{formatter,formatter+12,formatter+20,queue})
                self.assertFalse(vertex_producer_hooks(bytes(size),0x06004002))
                self.assertFalse(vertex_producer_hooks(bytes(size+1),0x06004000))

    def test_unknown_and_corrupt_images_do_not_get_hooks(self):
        self.assertFalse(vertex_producer_hooks(bytes(123940),0x06004000))

class RegisteredCallbacks(unittest.TestCase):
    def code(self,start=0):
        data=bytearray(96)
        words=[0xC707,0x6543,0x6403,0xD003,0x402B,0x0009]
        for i,w in enumerate(words):data[start+i*2:start+i*2+2]=w.to_bytes(2,'big')
        data[32:38]=bytes.fromhex('c704000b2041')
        return data

    def test_callback_and_pc_alignment_paths(self):
        for start in [0,2]:
            self.assertEqual(registered_store_callback_entries(self.code(start),0x6004000),{0x6004020})

    def test_requires_full_wrapper_body_and_in_image_store(self):
        original=self.code()
        for at in [2,4,6,8,10,32,34,36]:
            corrupt=bytearray(original);corrupt[at]^=1
            self.assertFalse(registered_store_callback_entries(corrupt,0x6004000))
        self.assertFalse(registered_store_callback_entries(original[:37],0x6004000))
        self.assertFalse(registered_store_callback_entries(original,0x6004001))
        outside=bytearray(original);outside[33]=255
        self.assertFalse(registered_store_callback_entries(outside,0x6004000))


class CopyEntries(unittest.TestCase):
    def setUp(self):
        # Encode the operation using independent SH-2 instruction field layouts.
        words = []
        for displacement in reversed(range(16)):
            words += [(5 << 12) | (5 << 4) | displacement,
                      (1 << 12) | (4 << 8) | displacement]
        self.code = b''.join(w.to_bytes(2, 'big') for w in words)

    def test_all_copy_lengths_and_both_return_slots(self):
        for slot in (0x0009, 0x200B):
            data = b'\x00\x09' + self.code + b'\x00\x0b' + slot.to_bytes(2, 'big')
            self.assertEqual(unrolled_copy_entries(data, 0x06001000),
                             set(range(0x06001002, 0x06001046, 4)))

    def test_partial_sequence_and_corrupted_register_are_rejected(self):
        self.assertFalse(unrolled_copy_entries(self.code, 0))
        corrupt = bytearray(self.code + b'\x00\x0b\x00\x09')
        corrupt[22] ^= 1
        self.assertFalse(unrolled_copy_entries(corrupt, 0))

    def test_unsafe_return_slot_and_unaligned_match_are_rejected(self):
        self.assertFalse(unrolled_copy_entries(self.code + b'\x00\x0b\x40\x0b', 0))
        self.assertFalse(unrolled_copy_entries(b'\x00' + self.code + b'\x00\x0b\x00\x09', 0))


class ShiftEntries(unittest.TestCase):
    def code(self, register=4):
        # Invented instruction sequence: signed shifts followed by signed narrowing.
        shift = 0x4021 + register * 256
        word = 0x4029 + register * 256
        byte = 0x4019 + register * 256
        extend_byte = 0x600E + register * 272
        extend_word = 0x600F + register * 272
        words = [shift]*6 + [word,byte,11,extend_byte] + [shift]*7 + [word,11,extend_word] + [shift]*14 + [11,shift,11,9]
        return b''.join(v.to_bytes(2,'big') for v in words)

    def test_complete_cascade_and_zero_shift_return(self):
        for register in (0,4,15):
            found = arithmetic_shift_entries(self.code(register), 0x6001000)
            self.assertIn(0x6001014, found) # Full word path, the observed class of target.
            self.assertIn(0x6001048, found) # Zero-shift return.
            self.assertNotIn(0x6001012, found) # Byte-extension delay slot.
            self.assertEqual(len(found),34)

    def test_partial_corrupt_and_unaligned_sequences_are_rejected(self):
        code=self.code()
        self.assertFalse(arithmetic_shift_entries(code[:-2], 0))
        self.assertFalse(arithmetic_shift_entries(b'\0'+code, 0))
        corrupt=bytearray(code);corrupt[44]^=1
        self.assertFalse(arithmetic_shift_entries(corrupt, 0))


if __name__ == '__main__':
    unittest.main()
