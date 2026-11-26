"""Import validated SFIII raw BIN, version-1 JSON YSBAK or Cafe-Alpha Vmem BUP saves without editing payloads."""
import argparse
import base64
import binascii
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import time


BUP_HEADER_SIZE = 64
BUP_MAGIC = b'Vmem'
BUP_MAX_COMMENT = 10
BUP_EPOCH = datetime.datetime(1980, 1, 1)


def bup_date_to_minutes(date):
    """Convert a Saturn BUP packed date to minutes since 1980-01-01."""
    year = 1980 + (date & 0x7f)
    month = (date >> 7) & 0xf
    day = (date >> 11) & 0x1f
    hour = (date >> 16) & 0x1f
    minute = (date >> 21) & 0x3f
    try:
        saved = datetime.datetime(year, month, day, hour, minute)
    except ValueError as e:
        raise ValueError('Invalid BUP date') from e
    return int((saved - BUP_EPOCH).total_seconds() // 60)


def minutes_to_bup_date(minutes):
    """Convert minutes since 1980-01-01 to a Saturn BUP packed date."""
    saved = BUP_EPOCH + datetime.timedelta(minutes=minutes)
    return ((saved.year - 1980) & 0x7f) | ((saved.month & 0xf) << 7) | ((saved.day & 0x1f) << 11) \
        | ((saved.hour & 0x1f) << 16) | ((saved.minute & 0x3f) << 21)


def bup_bytes(record):
    """Build a Cafe-Alpha Vmem .BUP file (64-byte big-endian header + payload) from a native record."""
    name = record[:12].split(b'\0')[0]
    comment = record[12:23].split(b'\0')[0][:BUP_MAX_COMMENT]
    language = record[23]
    date = struct.unpack_from('<I', record, 24)[0]
    payload = record[32:]
    packed = minutes_to_bup_date(date)
    head = bytearray(BUP_HEADER_SIZE)
    head[0:4] = BUP_MAGIC
    struct.pack_into('>I', head, 4, 0)
    head[16:16 + len(name)] = name
    head[28:28 + len(comment)] = comment
    head[39] = language
    struct.pack_into('>I', head, 40, packed)
    struct.pack_into('>I', head, 44, len(payload))
    struct.pack_into('>I', head, 52, packed)
    return bytes(head) + payload


def bup_record(path):
    """Parse a Vmem .BUP into (name, payload, metadata) without altering the payload."""
    data = path.read_bytes()
    if len(data) < BUP_HEADER_SIZE:
        raise ValueError('Truncated BUP header')
    head = data[:BUP_HEADER_SIZE]
    if head[:4] != BUP_MAGIC:
        raise ValueError('Not a Vmem BUP save')
    name = head[16:28].split(b'\0')[0].decode('ascii')
    comment = head[28:39].split(b'\0')[0].decode('ascii')
    language = head[39]
    date = struct.unpack_from('>I', head, 40)[0]
    size = struct.unpack_from('>I', head, 44)[0]
    if size > 524288 or size > len(data) - BUP_HEADER_SIZE:
        raise ValueError('Invalid BUP payload size')
    payload = data[BUP_HEADER_SIZE:BUP_HEADER_SIZE + size]
    saved = BUP_EPOCH + datetime.timedelta(minutes=bup_date_to_minutes(date))
    metadata = dict(filename=name, comment=comment, language=language,
                    saveDate=saved.strftime('%Y/%m/%d %H:%M:%S'), dataSize=len(payload))
    return name, payload, metadata


def records(data):
    result = {}
    while data:
        if len(data) < 32:
            raise ValueError('Truncated native backup header')
        head, data = data[:32], data[32:]
        name = head[:12].split(b'\0')[0].decode('ascii')
        size = struct.unpack_from('<I', head, 28)[0]
        if not name or name in result or size > 524288 or size > len(data):
            raise ValueError('Invalid or duplicate native backup record')
        result[name] = head + data[:size]
        data = data[size:]
    return result


def raw_record(path):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Save export exceeds the supported 16 MiB limit')
    data = path.read_bytes()
    metadata = None
    if path.suffix.lower() == '.ysbak':
        try:
            export = json.loads(data)
            if type(export['version']) is not int or export['version'] != 1:
                raise ValueError('Unsupported YSBAK version')
            metadata = export['metadata']
            name = metadata['filename']
            data = base64.b64decode(export['dataBase64'], validate=True)
            if type(metadata['dataSize']) is not int or metadata['dataSize'] != len(data):
                raise ValueError('YSBAK dataSize does not match its payload')
        except (KeyError, TypeError, UnicodeError, binascii.Error, json.JSONDecodeError) as e:
            raise ValueError('Invalid version-1 JSON YSBAK export') from e
    elif path.suffix.lower() == '.bup':
        try:
            name, data, metadata = bup_record(path)
        except (UnicodeError, ValueError) as e:
            raise ValueError('Invalid Vmem BUP save') from e
    elif path.suffix.lower() == '.bin':
        name = path.stem.upper()
    else:
        raise ValueError('Choose a raw BIN or version-1 JSON YSBAK save export')
    if not isinstance(name, str) or not re.fullmatch(r'SFORCE3[123]_[0-9]{2}', name):
        raise ValueError('Supported save names are SFORCE31_XX, SFORCE32_XX and SFORCE33_XX')
    expected = {'SFORCE31':12904, 'SFORCE32':16248, 'SFORCE33':24344}[name[:8]]
    if len(data) != expected or int.from_bytes(data[:4], 'big') != len(data)-8:
        raise ValueError(f'Unsupported save layout for {name}; expected {expected} bytes')
    if int.from_bytes(data[4:8], 'big') != sum(data[8:]):
        raise ValueError('SFIII payload checksum mismatch; original file was not changed')
    # Raw exports have no directory metadata. Date/comment below are import
    # metadata, not a reconstruction of the original Saturn directory entry.
    date = max(0, int((datetime.datetime.now()-datetime.datetime(1980, 1, 1)).total_seconds()/60))
    comment, language = b'Imported', 1
    if metadata is not None:
        try:
            comment = metadata['comment'].encode('ascii')
            language = metadata['language']
            saved = datetime.datetime.strptime(metadata['saveDate'], '%Y/%m/%d %H:%M:%S')
            date = int((saved-datetime.datetime(1980, 1, 1)).total_seconds()/60)
            if len(comment) > 11 or b'\0' in comment or type(language) is not int or not 0 <= language <= 5 or not 0 <= date <= 0xffffffff:
                raise ValueError('Invalid YSBAK directory metadata')
        except (KeyError, AttributeError, TypeError, UnicodeError) as e:
            raise ValueError('Invalid YSBAK directory metadata') from e
    head = name.encode().ljust(12, b'\0') + comment.ljust(11, b'\0') + bytes([language]) + struct.pack('<II', date, len(data))
    return name, head+data


def import_files(backup, device, sources, replace=False):
    backup = backup.resolve()
    for parent in [backup.parent, *backup.parents]:
        if (parent/'.git').exists():
            raise ValueError('Save data must stay outside the source repository')
    target = Path(str(backup)+('.cartridge' if device == 'cartridge' else ''))
    old = target.read_bytes() if target.exists() else b''
    existing = records(old)
    added, seen = [], set()
    for path in sources:
        path = path.resolve()
        if path == target:
            raise ValueError('Import source cannot be the native backup')
        name, record = raw_record(path)
        if name in seen or (name in existing and not replace):
            raise ValueError(f'{name} already exists; import was cancelled without changes')
        seen.add(name)
        existing[name] = record
        added.append(dict(name=name, source=str(path), bytes=len(record)-32,
                          payload_sha256=hashlib.sha256(record[32:]).hexdigest(),
                          source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    size, block = (32768, 64) if device == 'internal' else (524288, 512)
    used = sum(((len(v)-32+34+block-1)//block)+1 for v in existing.values())
    if used > size//block-2:
        raise ValueError('Not enough space on this device. Choose cartridge memory; no files were changed.')
    target.parent.mkdir(parents=True, exist_ok=True)
    stamp = str(time.time_ns())
    previous = Path(str(target)+'.before-import-'+stamp)
    if target.exists():
        with previous.open('xb') as f:
            f.write(old)
    output = b''.join(existing[k] for k in sorted(existing))
    fd, temporary = tempfile.mkstemp(prefix='save-import-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(output)
            f.flush()
            os.fsync(f.fileno())
        if (target.read_bytes() if target.exists() else b'') != old:
            raise ValueError('Backup changed during import. Close the game and retry.')
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    report = dict(device=device, target=str(target), previous=str(previous) if previous.exists() else None,
                  imported=added, used_blocks=used, available_blocks=size//block-2,
                  metadata='YSBAK directory metadata retained; raw BIN comment/date generated at import. Payloads unchanged.')
    Path(str(target)+'.import-'+stamp+'.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def export_files(backup, device, destination, bup=False):
    """Export ordinary Saturn saves as raw BIN (or Vmem BUP) payloads; never export native states."""
    target = Path(str(backup) + ('.cartridge' if device == 'cartridge' else ''))
    destination = destination.resolve()
    if any((p/'.git').exists() for p in (destination, *destination.parents)):
        raise ValueError('Save data must stay outside the source repository')
    entries = records(target.read_bytes())
    selected = {n:r for n,r in entries.items() if re.fullmatch(r'SFORCE3[123]_[0-9]{2}', n)}
    if not selected:
        raise ValueError('No ordinary SFIII saves found on this device. Save using the game menu first.')
    suffix = '.BUP' if bup else '.BIN'
    # Validate every payload before writing anything. Never overwrite earlier exports.
    for name, record in selected.items():
        payload = record[32:]
        expected = {'1':12904, '2':16248, '3':24344}[name[7]]
        if len(payload) != expected or int.from_bytes(payload[:4], 'big') != len(payload)-8 or int.from_bytes(payload[4:8], 'big') != sum(payload[8:]):
            raise ValueError('Invalid SFIII payload: ' + name)
        if (destination/(name+suffix)).exists():
            raise ValueError('Export file already exists; choose a new folder: ' + name)
    destination.mkdir(parents=True, exist_ok=True)
    result = []
    for name, record in sorted(selected.items()):
        path = destination/(name+suffix)
        blob = bup_bytes(record) if bup else record[32:]
        with path.open('xb') as stream:
            stream.write(blob)
        result.append(dict(name=name, path=str(path), bytes=len(record)-32,
                           payload_sha256=hashlib.sha256(record[32:]).hexdigest()))
    return dict(device=device, source=str(target), exported=result)


def main():
    ap = argparse.ArgumentParser(__doc__)
    ap.add_argument('--backup', type=Path, required=True)
    ap.add_argument('--device', choices=['internal', 'cartridge'], default='cartridge')
    ap.add_argument('--replace', action='store_true', help='Explicitly replace matching names; previous backup retained')
    ap.add_argument('--export', type=Path, metavar='DIRECTORY', help='Export ordinary saves as raw BIN files')
    ap.add_argument('--bup', action='store_true', help='Export as Cafe-Alpha Vmem .BUP files instead of raw BIN')
    ap.add_argument('files', nargs='*', type=Path)
    a = ap.parse_args()
    try:
        if not a.export and not a.files:
            ap.error('Choose import files or --export DIRECTORY')
        print(json.dumps(export_files(a.backup, a.device, a.export, a.bup) if a.export else import_files(a.backup, a.device, a.files, a.replace), indent=2))
    except (OSError, ValueError) as e:
        ap.exit(1, str(e)+'\n')


if __name__ == '__main__':
    main()
