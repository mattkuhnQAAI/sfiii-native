"""Attach optional locally generated artwork to one prepared disc revision.

Only small manifests are read at launch. Native pack loading checks
the complete startup index and retains original artwork for unmatched materials.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import uuid

MODEL_ENV = {'video': 'SF3_AI_VIDEO_DIR', 'anime': 'SF3_AI_ASSET_DIR',
             'general': 'SF3_AI_NATURAL_DIR'}
SELECTION = 'ai-packs.json'
# Preparation also copies this standalone helper beside player data. Only the
# original desktop/ module has an enclosing application/source root.
MODULE = Path(__file__).resolve()
SOURCE_ROOT = MODULE.parents[1] if MODULE.parent.name == 'desktop' and MODULE.name == 'ai_packs.py' else None


def read_json(path):
    path = Path(path)
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Artwork manifest is unexpectedly large: ' + str(path))
    return json.loads(path.read_text(encoding='utf-8-sig'))


def external(path):
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('Artwork and game-data paths must be absolute without parent traversal.')
    path = path.resolve(strict=True)
    if (SOURCE_ROOT is not None and (path == SOURCE_ROOT or SOURCE_ROOT in path.parents)) or any(
            (parent / '.git').exists() for parent in (path, *path.parents)):
        raise ValueError('Artwork and game data must stay outside a source repository.')
    return path


def disc_profile(game_dir, profiles_root):
    game_dir = external(game_dir)
    game = read_json(game_dir / 'game.json')
    if not isinstance(game, dict):
        raise ValueError('Prepared game metadata must be a JSON object.')
    identifier = game.get('profile_id')
    if not isinstance(identifier, str):
        raise ValueError('Prepare this game again to record its verified disc profile before attaching artwork.')
    records = [dict(read_json(path), profile_file=path.name)
               for path in sorted(Path(profiles_root).glob('*.json'))]
    matches = [record for record in records if record.get('id') == identifier]
    if len(matches) != 1 or not re.fullmatch(r'[0-9a-f]{64}', matches[0].get('content_sha256', '')):
        raise ValueError('The prepared game has no registered artwork disc profile.')
    return game_dir, matches[0]


def matches_profile(value, profile):
    # The disc inventory records the pinned profile filename; prepared games
    # record the profile's id. Both refer to this same authoritative document.
    return value in (profile['id'], profile['profile_file'])


def check_pack(path, profile, model, expected_count=None, expected_hash=None):
    path = external(path)
    if not path.is_dir():
        raise ValueError('Artwork pack is not a directory: ' + str(path))
    marker = path / 'manifest.json'
    manifest = read_json(marker)
    if not isinstance(manifest, dict):
        raise ValueError('Artwork manifest must be a JSON object.')
    if not matches_profile(manifest.get('profile'), profile) or manifest.get('revision') != profile['content_sha256']:
        raise ValueError('Artwork pack belongs to a different disc revision: ' + str(path))
    if manifest.get('scenario') != profile['scenario'] or manifest.get('model') != model:
        raise ValueError('Artwork scenario or style does not match its manifest.')
    count = manifest.get('material_count')
    if type(count) is not int or not 0 < count <= 100000 or (
            expected_count is not None and count != expected_count):
        raise ValueError('Artwork material count is invalid or changed.')
    digest = hashlib.sha256(marker.read_bytes()).hexdigest()
    if expected_hash is not None and expected_hash != digest:
        raise ValueError('Artwork manifest changed after attachment; attach the new pack explicitly.')
    # Startup indexes are regenerable optimization data. The native reader
    # validates them fully and falls back to per-material directory validation
    # when they are missing, stale or corrupt; attachment must preserve that.
    return str(path), count, digest


def validate_record(record, profile):
    if not isinstance(record, dict) or record.get('schema') != 1:
        raise ValueError('Unsupported artwork selection format.')
    if not matches_profile(record.get('profile'), profile) or record.get('revision') != profile['content_sha256']:
        raise ValueError('Selected artwork belongs to another prepared disc revision.')
    models = record.get('models')
    if not isinstance(models, dict) or set(models) != set(MODEL_ENV):
        raise ValueError('This runtime requires all three artwork styles: Animated, Illustrated and General.')
    counts, hashes = record.get('material_counts', {}), record.get('manifest_sha256', {})
    if not isinstance(counts, dict) or not isinstance(hashes, dict):
        raise ValueError('Invalid artwork selection metadata.')
    result = {}
    for model, variable in MODEL_ENV.items():
        if not isinstance(models[model], str):
            raise ValueError('Artwork pack path must be text.')
        result[variable] = check_pack(models[model], profile, model,
                                      counts.get(model), hashes.get(model))[0]
    return result


def validate_selection(game_dir, profiles_root):
    game_dir = external(game_dir)
    selected = game_dir / SELECTION
    if not selected.is_file():
        return {}
    _, profile = disc_profile(game_dir, profiles_root)
    return validate_record(read_json(selected), profile)


def backup_selection(selected):
    if selected.exists():
        destination = selected.with_name('ai-packs.previous-' + uuid.uuid4().hex + '.json')
        shutil.copy2(selected, destination)
        return destination
    return None


def install_selection(report, game_dir, profiles_root):
    game_dir, profile = disc_profile(game_dir, profiles_root)
    rows = read_json(external(report))
    if not isinstance(rows, list):
        raise ValueError('Select the builder or combined character packs-report.json file.')
    rows = [row for row in rows if isinstance(row, dict) and matches_profile(row.get('profile'), profile)]
    if len(rows) != 3 or {row.get('model') for row in rows} != set(MODEL_ENV):
        raise ValueError('The report must contain exactly one pack for each of the three styles for this disc profile.')
    record = dict(schema=1, profile=profile['id'], revision=profile['content_sha256'],
                  models={}, material_counts={}, manifest_sha256={})
    for row in rows:
        model = row['model']
        if row.get('revision') != profile['content_sha256']:
            raise ValueError('Artwork report belongs to another disc revision.')
        if type(row.get('material_count')) is not int or row['material_count'] < 1:
            raise ValueError('Artwork report has no valid material count.')
        path, count, digest = check_pack(row.get('path', ''), profile, model, row.get('material_count'))
        record['models'][model] = path
        record['material_counts'][model] = count
        record['manifest_sha256'][model] = digest
    validate_record(record, profile)
    selected = game_dir / SELECTION
    temporary = selected.with_name('.ai-packs-' + uuid.uuid4().hex + '.tmp')
    try:
        temporary.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        backup_selection(selected)
        os.replace(temporary, selected)
    finally:
        temporary.unlink(missing_ok=True)
    return record


def detach_selection(game_dir):
    selected = external(game_dir) / SELECTION
    backup = backup_selection(selected)
    if backup is not None:
        selected.unlink()
    return str(backup) if backup else None


def main():
    parser = argparse.ArgumentParser(__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('env', 'attach', 'detach'):
        command = commands.add_parser(name)
        command.add_argument('--game-dir', required=True, type=Path)
        if name != 'detach':
            command.add_argument('--profiles', required=True, type=Path)
        if name == 'attach':
            command.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'env':
            result = validate_selection(args.game_dir, args.profiles)
        elif args.command == 'attach':
            result = install_selection(args.report, args.game_dir, args.profiles)
        else:
            result = dict(previous_selection=detach_selection(args.game_dir))
        print(json.dumps(result))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('Optional artwork: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
