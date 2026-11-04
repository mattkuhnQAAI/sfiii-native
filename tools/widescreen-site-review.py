"""Compare an external routine across local editions without patching game data.

The manifest supplies reference {file, sha256, offset, length} and candidates
[{edition, file, sha256, base}]. All numbers are JSON integers. Reports contain
identities and offsets, never game bytes. A unique match is static evidence only.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def checked_file(entry, directory):
    path = (directory / entry['file']).resolve()
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != entry['sha256']:
        raise ValueError('Input identity mismatch: ' + str(path))
    return data, digest


def find_sites(data, signature, base):
    if type(base) is not int or base < 0 or base % 2 or base + len(data) > 2**32:
        raise ValueError('Invalid SH-2 load extent')
    if len(signature) < 32 or len(signature) % 2:
        raise ValueError('Reference must contain at least 16 whole instructions')
    hits = []
    start = 0
    while True:
        offset = data.find(signature, start)
        if offset < 0:
            break
        if offset % 2 == 0:
            hits.append(base + offset)
        start = offset + 1
    return hits


def review(manifest, directory):
    reference = manifest['reference']
    data, digest = checked_file(reference, directory)
    offset, length = reference['offset'], reference['length']
    if (type(offset) is not int or type(length) is not int or offset < 0 or
            offset % 2 or length < 32 or length % 2 or offset + length > len(data)):
        raise ValueError('Invalid reference instruction range')
    signature = data[offset:offset+length]
    rows, labels = [], set()
    for entry in manifest['candidates']:
        label = entry['edition']
        if not isinstance(label, str) or not label or label in labels:
            raise ValueError('Edition labels must be nonempty and unique')
        labels.add(label)
        candidate, sha = checked_file(entry, directory)
        hits = find_sites(candidate, signature, entry['base'])
        rows.append({'edition': label, 'module_sha256': sha,
                     'matches': [f'{hit:08X}' for hit in hits],
                     'status': 'unique-byte-match' if len(hits) == 1 else
                               'ambiguous' if hits else 'unmatched'})
    if not rows:
        raise ValueError('No candidate editions supplied')
    return {'reference_sha256': digest,
            'routine_sha256': hashlib.sha256(signature).hexdigest(),
            'routine_length': length, 'editions': rows,
            'limitations': 'Identical routine bytes do not verify callers, active overlay, '
                           'render-only use, behavior, or gameplay. No patch is enabled.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    output = args.out.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Write reports outside the source repository')
    result = review(json.loads(args.manifest.read_text(encoding='utf-8-sig')),
                    args.manifest.resolve().parent)
    output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    for row in result['editions']:
        print(row['edition'], row['status'], ', '.join(row['matches']))
    return 0 if all(row['status'] == 'unique-byte-match' for row in result['editions']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
