"""Check local projection/formatter routines against the native presentation hook.

Requires externally supplied BOOT files and a profile manifest. No game code,
assets or generated cases are distributed with this checker. The isolated SH-2
CPU executes the disc's original routine; it does not run gameplay or devices.
"""
import argparse
import hashlib
import json
import math
import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def external(path):
    result = Path(path).resolve()
    if result == REPO or REPO in result.parents:
        raise ValueError('Keep game images, cases and results outside the repository')
    return result


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--framework', type=Path, default=REPO/'.local/saturn-recomp')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(REPO/'tools'))
    from module_entries import vertex_hooks_for_identity, missing_vertex_hooks
    root = external(args.out)
    root.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(args.framework/'src'))
    from saturnrecomp.sh2emu import load, s32, MemoryError_
    from saturnrecomp.sh2 import decode
    profiles = json.loads(args.manifest.read_text())['profiles']
    results = []
    for profile in profiles:
        row = dict(edition=profile['edition'], passed=False)
        folder = root/profile['edition']
        folder.mkdir()
        try:
            image = external(profile['file'])
            data = image.read_bytes()
            assert hashlib.sha256(data).hexdigest() == profile['sha256'], 'BOOT identity changed'
            assert len(data) == profile['size'] and f'{zlib.crc32(data):08X}' == profile['crc32']
            if profile.get('generated'):
                generated = external(profile['generated'])
                import re
                files = sorted(set(re.findall(r'p_boot_\d+\.cpp',(generated/'CMakeLists.txt').read_text())))
                emitted = '\n'.join((generated/name).read_text() for name in files)
                hooks = vertex_hooks_for_identity(0x06004000,len(data),zlib.crc32(data))
                assert len(hooks) == 4 and not missing_vertex_hooks(emitted,hooks), 'An emitted producer path bypasses observation'
            cpu = load(str(image), 0x06004000)
            cpu.gbr = 0x06080000
            last, end = int(profile['last_projection'], 16), int(profile['last_end'], 16)
            formatter, queue = int(profile['formatter'], 16), int(profile['queue'], 16)
            near_entries = []
            for address in range(int(profile['projection'], 16), last, 2):
                ins = decode(cpu.rw(address), address)
                following = decode(cpu.rw(address+2), address+2)
                if ins.op == 'mov.b' and ins.disp == 173 and following.op == 'shlr' and following.n == 8:
                    near_entries.append(address)
            assert len(near_entries) == 1, 'Ambiguous near-depth prelude'
            cpu.wl(cpu.gbr+908, 512)
            cpu.wl(cpu.gbr+892, 1)
            rng = random.Random(0x534633)
            lines, points, quads = [], {}, {}

            def project(x, y, z, projection, near, ox=0, oy=0, address=0x06092000):
                cpu.wb(cpu.gbr+173, near)
                cpu.pc, cpu.steps, cpu.r[8] = near_entries[0], 0, projection
                while cpu.pc != near_entries[0]+12:
                    cpu.step()
                    assert cpu.steps < 100, 'Near-depth prelude did not terminate'
                assert cpu.r[8] == projection >> (near+2), 'Near-depth clamp differs from native recovery'
                ratio = (projection*65536)//max(z, projection >> (near+2))
                assert 0 < ratio < 0x80000000
                cpu.pc, cpu.steps = last, 0
                cpu.r[4], cpu.r[5], cpu.r[7] = x & 0xffffffff, y & 0xffffffff, ratio
                cpu.r[11], cpu.r[12], cpu.r[13] = address, 1025, 1025
                cpu.ww(cpu.gbr+148, ox)
                cpu.ww(cpu.gbr+150, oy)
                cpu.wl(address+12, z)
                while cpu.pc != end:
                    cpu.step()
                    assert cpu.steps < 2000, 'Projection escaped its reviewed range'
                return tuple(cpu.rl(address+i*4) for i in range(4))

            for case in range(1800):
                projection = rng.choice([32, 64, 160, 256])*65536
                near = rng.choice([1, 2, 3, 4])
                minimum = projection >> (near+2)
                z = rng.choice([max(1, minimum//2), minimum, minimum+1, projection, projection*4])
                ratio = (projection*65536)//max(z, minimum)
                extent = 100 if case < 900 else 1800
                x, y = (round(rng.uniform(-extent, extent)*4294967296/ratio) for _ in range(2))
                assert abs(x) < 0x80000000 and abs(y) < 0x80000000
                ox, oy = rng.choice([(0, 0), (13, -7), (-9, 11)])
                record = project(x, y, z, projection, near, ox, oy)
                packed, stored_x, stored_y, _ = record
                native = (s32(packed) >> 16, ((packed & 65535)^32768)-32768)
                # The origin participates in visibility tests. The point record
                # and command remain local coordinates; adding it here is wrong.
                precise = (s32(stored_x & ~1)*ratio/4294967296, s32(stored_y)*ratio/4294967296)
                direct = tuple(math.floor(v) for v in precise) == native
                points[case] = dict(native=native, precise=precise, direct=direct)
                lines.append('P '+str(case)+' '+' '.join(map(str,record+(projection,near,ox,oy))))
            # Execute every actual formatter entry and both lighting flags on
            # convex invented quads. The original routine supplies corner order.
            for entry in [0, 12, 20]:
                for flag in [0, 0x40000]:
                    for case in range(12):
                        records = []
                        for i, (x, y) in enumerate([(-10,-6),(15,-6),(16,13),(-9,13)]):
                            records.append(project(round((x+rng.uniform(.1,.8))*65536),round((y+rng.uniform(.1,.8))*65536),65536,65536,2,address=0x06091000+i*16))
                        cpu.pc, cpu.steps, cpu.pr = formatter+entry, 0, queue
                        cpu.r[11], cpu.r[8], cpu.r[15], cpu.r[5] = 0x06090000, 0x06093000, 0x060f0000, flag
                        cpu.r[9] = 0x06095000
                        cpu.wl(cpu.r[9], queue)
                        for i in range(4):cpu.r[i+1] = 0x06091000+i*16
                        for i in range(32):cpu.wb(cpu.r[11]+i, 0)
                        cpu.ww(cpu.r[11], 2)
                        cpu.ww(cpu.r[8]+2, 0)
                        cpu.wl(cpu.r[8]+4, flag)
                        cpu.ww(cpu.r[8]+8, 0)
                        cpu.wl(cpu.r[15]+20, 0x06094000)
                        cpu.wl(0x06094000, 0)
                        retained = (tuple(cpu.r[1:5]),cpu.r[11],cpu.gbr,cpu.pr)
                        cpu.step()
                        assert retained == (tuple(cpu.r[1:5]),cpu.r[11],cpu.gbr,cpu.pr), 'Entry instruction changes observer inputs'
                        while cpu.pc != queue:
                            try:cpu.step()
                            except Exception as error:
                                raise RuntimeError(f'Formatter entry {entry}, flag {flag:X}, PC {cpu.pc:08X}: {error}') from error
                            assert cpu.steps < 1000, 'Formatter did not return to the reviewed queue'
                        command = bytes(cpu.rb(cpu.r[11]+i) for i in range(32))
                        corners = struct.unpack('>4I', command[12:28])
                        assert sorted(corners) == sorted(r[0] for r in records), 'Formatter introduced an unknown corner'
                        ident = len(quads)
                        quads[ident] = dict(entry=entry, flag=flag)
                        fields = [entry,65536,2]+[v for record in records for v in record]
                        lines.append('Q '+str(ident)+' '+' '.join(map(str,fields))+' '+command.hex())
            fixture = folder/'cases.txt'
            fixture.write_text('\n'.join(lines)+'\n')
            output = folder/'actual.txt'
            run = subprocess.run([str(external(args.runner)), str(image), str(fixture), str(output)], capture_output=True, text=True, timeout=90)
            (folder/'runner.log').write_text(run.stdout+run.stderr)
            assert run.returncode == 0, 'Native hook test failed; inspect runner.log'
            direct_passed = ranged_passed = quads_passed = 0
            seen = set()
            for line in output.read_text().splitlines():
                fields = line.split()
                kind, ident, accepted = fields[0], int(fields[1]), int(fields[2])
                assert (kind, ident) not in seen, 'Duplicate runner output'
                seen.add((kind, ident))
                if kind == 'P':
                    point = points[ident]
                    if accepted:
                        values = tuple(map(float, fields[3:5]))
                        assert all(math.isfinite(v) for v in values)
                        assert tuple(math.floor(v) for v in values) == point['native'], 'Native pixel changed'
                        if point['direct']:
                            assert max(abs(a-b) for a,b in zip(values, point['precise'])) < .0002, 'Fraction differs from original fixed-point projection'
                            direct_passed += 1
                        else:ranged_passed += 1
                else:
                    assert kind == 'Q' and ident in quads and accepted, 'Verified formatter submission was not refined'
                    quads_passed += 1
            assert len(seen) == len(points)+len(quads), 'Missing runner cases'
            row.update(projection_cases=len(points), direct_accepted=direct_passed, range_limited_accepted=ranged_passed, formatter_cases=quads_passed)
            assert direct_passed >= 850 and ranged_passed >= 25 and quads_passed == 72, 'Insufficient condition coverage'
            assert hashlib.sha256(image.read_bytes()).hexdigest() == profile['sha256'], 'Source BOOT was modified'
            row.update(passed=True, projection_cases=len(points), direct_accepted=direct_passed, range_limited_accepted=ranged_passed, formatter_cases=quads_passed, boot_sha256=profile['sha256'], fixtures_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest())
        except (OSError, AssertionError, ValueError, RuntimeError, MemoryError_, subprocess.TimeoutExpired) as error:
            row['error'] = str(error)
        results.append(row)
        print(('PASS' if row['passed'] else 'FAIL')+': '+row['edition'], flush=True)
    report = dict(passed=bool(results) and all(r['passed'] for r in results), results=results, complete_game_certified=False, scope='Isolated original projection and formatter execution on invented inputs, real native hook guards, and native integer-cell preservation. No gameplay or full hardware certification.')
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
