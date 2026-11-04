"""Inspect a VDP1 command list from a local video dump or execution trace.

This reconstructs a list from captured VRAM; it is not a trace of commands
actually executed for the displayed framebuffer. Reports stay external.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import struct

VRAM_SIZE = 0x80000
DUMP_SIZE = 0x141220
ROOT = Path(__file__).resolve().parents[1]


def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def horizontal(points, width):
    xmin, xmax = min(p[0] for p in points), max(p[0] for p in points)
    return ('outside_left' if xmax < 0 else 'outside_right' if xmin >= width
            else 'spans_both' if xmin < 0 and xmax >= width
            else 'crosses_left' if xmin < 0 else 'crosses_right' if xmax >= width else 'inside')


def analyze(data, width=320, guard=20000):
    if len(data) != DUMP_SIZE:
        raise ValueError('Expected an exact runtime video dump')
    if width <= 0 or width > 1024 or guard <= 0:
        raise ValueError('Invalid viewport or traversal limit')
    vram = data[:VRAM_SIZE]
    read = lambda offset: struct.unpack_from('>H', vram, offset & (VRAM_SIZE - 1))[0]
    address = return_address = 0
    local_x = local_y = 0
    system_clip = [None, None]
    user_clip = [None, None, None, None]
    counts = Counter()
    quads = []
    seen = set()
    status = 'guard_exhausted'
    for _ in range(guard):
        # Include return and coordinate state: loops can revisit a command with different state.
        state = (address, return_address, local_x, local_y, *system_clip, *user_clip)
        if state in seen:
            status = 'cycle_detected'
            break
        seen.add(state)
        ctrl = read(address)
        if ctrl & 0x8000:
            status = 'end'
            break
        jump = (ctrl >> 12) & 7
        kind = ctrl & 15
        if not jump & 4:
            counts[str(kind)] += 1
            if kind in (8, 11):
                user_clip = [read(address + 12) & 0x3ff, read(address + 14) & 0x1ff,
                             read(address + 20) & 0x3ff, read(address + 22) & 0x1ff]
            elif kind == 9:
                system_clip = [read(address + 20) & 0x3ff, read(address + 22) & 0x1ff]
            elif kind == 10:
                local_x, local_y = signed(read(address + 12), 11), signed(read(address + 14), 11)
            elif kind in (2, 3, 4):
                # These four-vertex primitives have explicit coordinates. Normal and
                # scaled sprites use different geometry and are intentionally excluded.
                points = [[signed(read(address + 12 + 4*i), 13) + local_x,
                           signed(read(address + 14 + 4*i), 13) + local_y] for i in range(4)]
                quads.append({'address': address, 'kind': kind, 'points': points,
                    'horizontal': horizontal(points, width),
                    'texture_address': read(address + 8) * 8,
                    'texture_size_word': read(address + 10), 'draw_mode': read(address + 4),
                    'system_clip': system_clip.copy(), 'user_clip': user_clip.copy()})
        link = (read(address + 2) * 8) & (VRAM_SIZE - 1)
        action = jump & 3
        if action == 0:
            address += 32
        elif action == 1:
            address = link
        elif action == 2:
            return_address, address = address + 32, link
        else:
            address = return_address
        address &= VRAM_SIZE - 1
    return {'status': status, 'width': width, 'dump_sha256': hashlib.sha256(data).hexdigest(),
            'command_counts': dict(sorted(counts.items())),
            'quad_edge_counts': dict(sorted(Counter(q['horizontal'] for q in quads).items())),
            'quads': quads,
            'limitations': 'VRAM reconstruction only, not proof of executed or visible objects; '
                           'timing limits, unsupported primitives and guest-side omissions are not traced.'}


def analyze_trace(stream, width=320):
    """Analyze commands captured at execution, grouped by field and draw."""
    if width <= 0 or width > 1024:
        raise ValueError('Invalid viewport width')
    groups = {}
    reader = csv.DictReader(stream)
    required = {'field', 'draw', 'address', 'control', 'local_x', 'local_y',
                'sys_x', 'sys_y', 'ux0', 'uy0', 'ux1', 'uy1', 'raw_words'}
    if not reader.fieldnames or not required.issubset(reader.fieldnames):
        raise ValueError('Invalid visibility trace header')
    for row in reader:
        field, draw = int(row['field']), int(row['draw'])
        raw = bytes.fromhex(row['raw_words'])
        if len(raw) != 32:
            raise ValueError('Expected 16 command words')
        words = struct.unpack('>16H', raw)
        if words[0] != int(row['control']):
            raise ValueError('Trace control does not match command')
        group = groups.setdefault((field, draw), {'field': field, 'draw': draw, 'commands': 0, 'quads': []})
        group['commands'] += 1
        kind = words[0] & 15
        if kind not in (2, 3, 4):
            continue
        lx, ly = int(row['local_x']), int(row['local_y'])
        points = [[signed(words[6+2*i], 13)+lx, signed(words[7+2*i], 13)+ly] for i in range(4)]
        group['quads'].append({'address': int(row['address']), 'kind': kind, 'points': points,
            'horizontal': horizontal(points, width), 'texture_address': words[4]*8,
            'texture_size_word': words[5], 'draw_mode': words[2],
            'system_clip': [int(row['sys_x']), int(row['sys_y'])],
            'user_clip': [int(row[key]) for key in ('ux0', 'uy0', 'ux1', 'uy1')]})
    if not groups:
        raise ValueError('Trace contains no executed commands')
    draws = list(groups.values())
    for draw in draws:
        draw['quad_edge_counts'] = dict(sorted(Counter(q['horizontal'] for q in draw['quads']).items()))
    return {'status': 'executed_trace', 'width': width, 'draws': draws,
            'limitations': 'Records executed VDP1 commands, not rejected game objects or proof of final visibility; '
                           'normal/scaled sprites and other primitive types are excluded from quad counts.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path, nargs='?')
    parser.add_argument('--trace', type=Path, help='Executed-command CSV from an instrumented local runtime')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--width', type=int, default=320)
    args = parser.parse_args()
    if (args.dump is None) == (args.trace is None):
        parser.error('Provide exactly one video dump or executed-command trace')
    output = args.out.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Write reports outside the source repository')
    if args.trace:
        with args.trace.open(encoding='utf-8', newline='') as stream:
            result = analyze_trace(stream, args.width)
    else:
        result = analyze(args.dump.read_bytes(), args.width)
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    summary = {k: v for k, v in result.items() if k not in ('quads', 'draws')}
    if 'draws' in result:
        summary['draw_count'] = len(result['draws'])
        summary['executed_commands'] = sum(d['commands'] for d in result['draws'])
    print(json.dumps(summary, indent=2))
    return 0 if result['status'] in ('end', 'executed_trace') else 1


if __name__ == '__main__':
    raise SystemExit(main())
