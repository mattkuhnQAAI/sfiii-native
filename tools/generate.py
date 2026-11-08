"""Generate local game code from an evidence-based module profile."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from module_entries import unrolled_copy_entries, arithmetic_shift_entries, registered_store_callback_entries, vertex_producer_hooks
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
manifest = verify_inventory(work, profile)
specs = []
presentation_hooks = {}
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
    if m['name'] == 'BOOT':
        presentation_hooks[m['name']] = sorted(vertex_producer_hooks(data, base))
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
    specs.append((m['name'], str(source), base, sorted(seeds)))
overlays = {m['name'] for m in modules if m.get('overlay')}
if a.framework == 'saturnkit':
    sys.path.insert(0, str(repo / '.local'))
    from saturnkit.recomp.__main__ import generate
    generate(specs, str(out), overlays=overlays, optest=a.optest)
else:
    sys.path.insert(0, str(repo / '.local' / 'saturn-recomp' / 'src'))
    from saturnrecomp.recomp.__main__ import generate
    generate(specs, str(out), callable_entries=overlays,
             hooks=presentation_hooks,
             runtime_literals={m['name'] for m in modules}, optest=a.optest, native_states=True,
             stack_return_calls={m['name']:{int(pc,16) for pc in m.get('stack_return_calls',[])} for m in modules})
