"""Validate invented native/4K PNG fixtures with independent zlib and CRC checks."""
from pathlib import Path
import struct
import sys
import zlib

root=Path(sys.argv[1])
for w,h in [(1,1),(320,240),(427,240),(3843,2160)]:
    content=(root/f'{w}x{h}.png').read_bytes()
    assert content[:8]==b'\x89PNG\r\n\x1a\n'
    at=8;compressed=bytearray();kinds=[]
    while at<len(content):
        length=struct.unpack_from('>I',content,at)[0];kind=content[at+4:at+8]
        body=content[at+8:at+8+length];crc=struct.unpack_from('>I',content,at+8+length)[0]
        assert zlib.crc32(kind+body)==crc
        kinds.append(kind)
        if kind==b'IHDR':assert struct.unpack('>IIBBBBB',body)==(w,h,8,2,0,0,0)
        if kind==b'IDAT':compressed.extend(body)
        at+=12+length
    assert at==len(content) and kinds==[b'IHDR',b'IDAT',b'IEND']
    decoded=zlib.decompress(compressed);assert len(decoded)==(w*3+1)*h
    for y in range(h):
        row=decoded[y*(w*3+1):(y+1)*(w*3+1)];assert row[0]==0
        # Every scanline and wide-row boundaries are checked independently;
        # zlib additionally verifies the complete decoded Adler checksum.
        for x in {0,1 if w>1 else 0,w//2,w-1,(y*131)%w}:
            assert row[1+x*3:4+x*3]==bytes(((x*17+y*13)&255,(x*7+y*19)&255,(x*23+y*3)&255))
print('PASS: independent PNG CRC/Adler/deflate, scanlines and RGB samples through 2160p widescreen')
