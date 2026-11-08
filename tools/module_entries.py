"""Conservative recognition of entry points in fully matched SH-2 code idioms."""


def registered_store_callback_entries(data: bytes, base: int) -> set[int]:
    """PC-relative callback passed to a registration tail-call, then halfword store.

    Require both the complete registration wrapper and its small callback body.
    This avoids treating arbitrary MOVA data pointers as executable entries.
    """
    entries = set()
    word = lambda at: int.from_bytes(data[at:at+2], 'big')
    for at in range(0,len(data)-11,2):
        if (base+at)&1 or word(at)&0xFF00 != 0xC700:continue
        if [word(at+n) for n in (2,4,8,10)] != [0x6543,0x6403,0x402B,0x0009]:continue
        if word(at+6)&0xFF00 != 0xD000:continue
        target=((base+at+4)&~3)+(word(at)&255)*4
        literal=((base+at+10)&~3)+(word(at+6)&255)*4
        off=target-base
        if not (0<=off<=len(data)-6 and base<=literal<=base+len(data)-4):continue
        if word(off)&0xFF00 != 0xC700 or [word(off+2),word(off+4)] != [0x000B,0x2041]:continue
        destination=((target+4)&~3)+(word(off)&255)*4
        if base<=destination<=base+len(data)-2:entries.add(target)
    return entries


def unrolled_copy_entries(data: bytes, base: int) -> set[int]:
    """Entry per longword in a complete descending 64-byte copy and return.

    Some routines compute a jump into this sequence according to copy length.
    Require all sixteen load/store pairs and a known harmless RTS delay slot;
    neither a lone instruction pair nor an arbitrary pointer is evidence.
    """
    pattern = b''.join(
        (0x5050 | n).to_bytes(2, 'big') + (0x1400 | n).to_bytes(2, 'big')
        for n in range(15, -1, -1)
    )
    entries = set()
    for offset in range(0, len(data) - 67, 2):
        if (base + offset) & 1:
            continue
        if data[offset:offset + 64] != pattern:
            continue
        if data[offset + 64:offset + 68] not in (b'\x00\x0b\x00\x09', b'\x00\x0b\x20\x0b'):
            continue
        # Include the RTS entry for a zero-length copy as well.
        entries.update(base + offset + n * 4 for n in range(17))
    return entries


def arithmetic_shift_entries(data: bytes, base: int) -> set[int]:
    """Entries in a complete byte/word/longword arithmetic-right-shift cascade.

    Count dispatch jumps into SHAR chains; register only verified instructions,
    including RTS entries whose delay slot performs the final operation.
    Require all three chains and the zero-shift return, not isolated shifts.
    """
    entries = set()
    for register in range(16):
        shar = 0x4021 | register << 8
        words = ([shar] * 6 + [0x4029 | register << 8, 0x4019 | register << 8,
                              0x000B, 0x600E | register << 8 | register << 4]
                 + [shar] * 7 + [0x4029 | register << 8, 0x000B, 0x600F | register << 8 | register << 4]
                 + [shar] * 14 + [0x000B, shar, 0x000B, 0x0009])
        pattern = b''.join(w.to_bytes(2, 'big') for w in words)
        at = data.find(pattern)
        while at >= 0:
            if not ((base + at) & 1):
                entries.update(base + at + off for off in range(0, len(pattern), 2)
                               if off not in (18, 38, 70, 74))
            at = data.find(pattern, at + 1)
    return entries
def vertex_hooks_for_identity(base: int, size: int, crc: int) -> set[int]:
    """Observe reviewed formatter/queue instructions, including internal jumps.

    Their first instructions change only r0/r5, leaving point pointers, command
    pointer, GBR and PR intact for the presentation observer. Unknown BOOT
    revisions must acquire their own producer review before emission is enabled.
    """
    profiles = {
        (117672, 0xEBEEDDFA): (0x06017144, 0x06016700),
        (123940, 0xCC57FA60): (0x06016A38, 0x06015FEC),
        (123872, 0x40B12530): (0x060169F0, 0x06015F9C),
        (124272, 0xA9FE4141): (0x06016A70, 0x06015FE4),
    }
    selected = profiles.get((size, crc)) if base == 0x06004000 else None
    if selected is None:return set()
    formatter, queue = selected
    return {formatter, formatter+12, formatter+20, queue}


def vertex_producer_hooks(data: bytes, base: int) -> set[int]:
    import zlib
    return vertex_hooks_for_identity(base, len(data), zlib.crc32(data))


def missing_vertex_hooks(source: str, hooks: set[int]) -> list[int]:
    """Check every emitted copy, including branches that stay in one function."""
    import re
    missing = []
    for address in sorted(hooks):
        lines = re.findall(rf'(?m)^[^\n]*// {address:08X} [^\n]*$', source)
        marker = f'sh2_hook(c, 0x{address:08X}u);'
        if not lines or any(marker not in line for line in lines):missing.append(address)
    return missing
