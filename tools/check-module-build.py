"""Check that every audited module and discovered entry was emitted, outside source."""
import argparse
import json
from pathlib import Path
import re
import sys
from importlib.util import spec_from_file_location, module_from_spec

# Executing a sibling by file path does not always add its directory to the
# module search path (notably with embedded Python distributions).
sys.path.insert(0, str(Path(__file__).resolve().parent))
from module_entries import vertex_hooks_for_identity, missing_vertex_hooks
spec = spec_from_file_location('audit_modules',Path(__file__).with_name('audit-modules.py'))
audit = module_from_spec(spec)
spec.loader.exec_module(audit)

ap = argparse.ArgumentParser(__doc__)
ap.add_argument('--audit',type=Path,required=True)
ap.add_argument('--generated',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--require-complete',action='store_true',help='Also fail on unresolved static audit findings')
ap.add_argument('--player',action='store_true',help='Reject synthetic instruction-test modules in player builds')
a = ap.parse_args()
generated, out = audit.external(a.generated), audit.external(a.out)
if a.player and (generated/'p_optest_table.cpp').exists():
    raise SystemExit('Synthetic instruction-test modules must not be included in a player build')
report = json.loads(a.audit.read_text())
profile = json.loads(a.audit.with_name('candidate-profile.json').read_text())
names = {m['file']:m['name'] for m in profile['modules']}
missing, entries = [], 0
for row in report['modules']:
    if 'entry_points' not in row:
        continue
    name = names[row['file']]
    table = generated/f'p_{name.lower()}_table.cpp'
    actual = set(re.findall(r'\{0x([0-9A-Fa-f]{8})u,\s*f_',table.read_text())) if table.exists() else set()
    expected = set(row['entry_points'])
    absent = sorted(expected-{e.upper() for e in actual})
    entries += len(expected)
    if absent:missing.append(dict(module=name,entries=absent))
    if name == 'BOOT' and table.exists():
        identity = re.search(r'SH2Module module\s*=\s*\{"BOOT",\s*0x([0-9A-Fa-f]+)u,\s*(\d+)u,\s*0x([0-9A-Fa-f]+)u',table.read_text())
        if identity:
            base,size,crc = int(identity[1],16),int(identity[2]),int(identity[3],16)
            hooks = vertex_hooks_for_identity(base,size,crc)
            files = sorted(set(re.findall(r'p_boot_\d+\.cpp',(generated/'CMakeLists.txt').read_text())))
            source = '\n'.join((generated/name).read_text() for name in files)
            unobserved = missing_vertex_hooks(source,hooks)
            if unobserved:missing.append(dict(module=name,presentation_hooks=[f'{address:08X}' for address in unobserved]))
blockers = dict(mapping_or_entry=len(report['blockers']),
                indirect_sites=sum(r.get('discovery',{}).get('unresolved indirect',0) for r in report['modules']),
                discovery_problems=sum(len(r.get('problems',[])) for r in report['modules']),
                cross_module_records=len(report['cross_module_gaps']))
complete = not missing and not any(blockers.values())
result = dict(modules=len(names),expected_entries=entries,missing=missing,
              static_findings=blockers,complete_static_audit=complete,
              caveat='Emission check only. Does not certify discovery completeness or gameplay.')
out.write_text(json.dumps(result,indent=2)+'\n')
print(f'{len(names)} modules, {entries} audited entries, {len(missing)} modules with missing emitted entries')
if a.require_complete and not complete:print('Full static audit gate FAILED: unresolved findings remain')
raise SystemExit(bool(missing) or (a.require_complete and not complete))
