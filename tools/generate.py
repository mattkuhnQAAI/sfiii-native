"""Generate local game code from an evidence-based module profile."""
import argparse
import json
from pathlib import Path
import sys
from module_entries import unrolled_copy_entries, arithmetic_shift_entries, registered_store_callback_entries
from disc_profile import verify_inventory
from module_images import expand_modules, verified_source, image_bytes

p = argparse.ArgumentParser(__doc__)
p.add_argument('--framework', choices=['saturnkit', 'saturn-recomp'], required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--profile', type=Path, default=Path('profiles/usa-v1.001.json'))
p.add_argument('--optest', action='store_true')
p.add_argument('--out', type=Path, help='External generated-code directory; defaults to WORK/recomp[-saturnkit]')
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
work = a.work.resolve()
out = (a.out or work / ('recomp-saturnkit' if a.framework == 'saturnkit' else 'recomp')).resolve()
if work == repo or repo in work.parents or out == repo or repo in out.parents:
    raise SystemExit('Game-derived output must be outside the repository')
profile = json.loads(a.profile.read_text())
scene_rule = None
if a.framework == 'saturn-recomp':
    sys.path.insert(0, str(repo / '.local' / 'saturn-recomp' / 'src'))
    from saturnrecomp.sh2 import Image
    from saturnrecomp.recomp.discover import Program
    from scene_data_entries import is_scene, data_table_entries
    scene_rule, scene_log = True, []
manifest = verify_inventory(work, profile)
specs = []
modules = expand_modules(profile, manifest)
for m in modules:
    source, original = verified_source(work, m, manifest)
    image = image_bytes(m, original)
    if 'bytes' in m or 'offset' in m:
        prepared = work / 'modules' / m['file']
        prepared.parent.mkdir(exist_ok=True)
        prepared.write_bytes(image)
        source = prepared
    base = int(m['base'], 16)
    seeds = {int(s, 16) for s in m['seeds']}
    data = source.read_bytes()
    seeds.update(unrolled_copy_entries(data, base))
    seeds.update(arithmetic_shift_entries(data, base))
    seeds.update(registered_store_callback_entries(data, base))
    # Common SFIII export veneer: mov.l @(PC),r0; jmp @r0; nop; nop; target.
    # Only aligned veneers whose literal points within this verified image qualify.
    for at in range(0, len(data) - 11, 4):
        if data[at:at+8] == bytes.fromhex('d001402b00090009'):
            target = int.from_bytes(data[at+8:at+12], 'big')
            if base <= target < base + len(data) and target % 2 == 0:
                seeds.add(base + at)
    if scene_rule and is_scene(m['name']):
        # Callbacks a scene keeps only in its own data tables (tools/scene_data_entries.py).
        image_view = Image(data, base)
        found, rows = data_table_entries(image_view, Program(image_view, [base] + sorted(seeds - {base})), m['name'])
        seeds.update(found)
        scene_log += [(m['name'], t, v, why, refs) for t, v, why, refs in rows]
        rejected = sum(1 for row in rows if row[1] == 'rejected')
        print('%s: scene data-table entries: %d accepted, %d rejected' % (m['name'], len(found), rejected), flush=True)
    specs.append((m['name'], str(source), base, sorted(seeds)))
if scene_rule:
    out.mkdir(parents=True, exist_ok=True)
    with open(out / 'scene-data-entries.tsv', 'w') as log:
        log.write('module\ttarget\tverdict\treason\ttable_words\n')
        for name, t, v, why, refs in scene_log:
            log.write('%s\t%08X\t%s\t%s\t%s\n' % (name, t, v, why, ' '.join('%08X' % r for r in refs)))
overlays = {m['name'] for m in modules if m.get('overlay')}
if a.framework == 'saturnkit':
    sys.path.insert(0, str(repo / '.local'))
    from saturnkit.recomp.__main__ import generate
    generate(specs, str(out), overlays=overlays, optest=a.optest)
else:
    sys.path.insert(0, str(repo / '.local' / 'saturn-recomp' / 'src'))
    from saturnrecomp.recomp.__main__ import generate
    generate(specs, str(out), callable_entries=overlays,
             runtime_literals={m['name'] for m in modules}, optest=a.optest, native_states=True,
             stack_return_calls={m['name']:{int(pc,16) for pc in m.get('stack_return_calls',[])} for m in modules})
