import importlib.util
import base64
import datetime
import json
import struct
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('save_import', Path(__file__).resolve().parents[1]/'import-saturn-save.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ImportTests(unittest.TestCase):
    def test_export_roundtrip_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); archive,obj,data=self.archive(root);backup=root/'backup.bin'
            module.import_files(backup,'cartridge',[archive])
            before=Path(str(backup)+'.cartridge').read_bytes()
            result=module.export_files(backup,'cartridge',root/'export')
            exported=Path(result['exported'][0]['path'])
            self.assertEqual(exported.read_bytes(),data)
            self.assertEqual(module.raw_record(exported)[1][32:],data)
            self.assertEqual(Path(str(backup)+'.cartridge').read_bytes(),before)
            with self.assertRaisesRegex(ValueError,'already exists'):module.export_files(backup,'cartridge',root/'export')
            with self.assertRaises(FileNotFoundError):module.export_files(backup,'internal',root/'empty')

    def test_scenario3_raw_and_archive_preserve_payloads_with_cartridge_capacity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data=(24336).to_bytes(4,'big')+(255).to_bytes(4,'big')+b'\xff'+bytes(24335)
            raw=root/'SFORCE33_01.BIN';raw.write_bytes(data)
            archive,obj,_=self.archive(root)
            obj['metadata'].update(filename='SFORCE33_02',dataSize=len(data),comment='Julian')
            obj['dataBase64']=base64.b64encode(data).decode();archive.write_text(json.dumps(obj))
            backup=root/'backup.bin'
            module.import_files(backup,'internal',[raw]);previous=backup.read_bytes()
            with self.assertRaisesRegex(ValueError,'Not enough space'):module.import_files(backup,'internal',[archive])
            self.assertEqual(backup.read_bytes(),previous)
            module.import_files(backup,'cartridge',[raw,archive])
            entries=module.records(Path(str(backup)+'.cartridge').read_bytes())
            self.assertEqual(set(entries),{'SFORCE33_01','SFORCE33_02'})
            for record in entries.values():self.assertEqual(record[32:],data)
            self.assertEqual(raw.read_bytes(),data)
            raw.write_bytes(data[:-1]+b'\x01')
            with self.assertRaisesRegex(ValueError,'checksum mismatch'):module.raw_record(raw)

    def test_scenario3_rejects_other_scenario_layouts_and_invalid_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            data=(16240).to_bytes(4,'big')+bytes(16244)
            for name in ('SFORCE33_01','SFORCE34_01','SFORCE33_001'):
                raw=root/(name+'.BIN');raw.write_bytes(data)
                with self.subTest(name=name),self.assertRaises(ValueError):module.raw_record(raw)

    def archive(self, root):
        data = (16240).to_bytes(4,'big') + (7).to_bytes(4,'big') + b'\x07' + bytes(16239)
        obj = dict(version=1, metadata=dict(filename='SFORCE32_03', comment='Medion', language=0,
                   saveDate='2026/10/20 13:52:00', dataSize=len(data)),
                   dataBase64=base64.b64encode(data).decode(), screenshotBase64='ignored')
        path = root/'renamed.ysbak';path.write_text(json.dumps(obj))
        return path, obj, data

    def test_scenario2_archive_preserves_metadata_and_ignores_screenshot_url(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); path,obj,data=self.archive(root)
            original=path.read_bytes()
            obj['metadata']['screenshotUrl']='https://example.invalid/never-fetch'
            path.write_text(json.dumps(obj)); original=path.read_bytes()
            backup=root/'backup.bin'
            module.import_files(backup,'cartridge',[path])
            record=module.records(Path(str(backup)+'.cartridge').read_bytes())['SFORCE32_03']
            self.assertEqual(record[32:],data)
            self.assertEqual(record[:24],b'SFORCE32_03\0Medion\0\0\0\0\0\0')
            date=int((datetime.datetime(2026,10,20,13,52)-datetime.datetime(1980,1,1)).total_seconds()/60)
            self.assertEqual(struct.unpack_from('<II',record,24),(date,16248))
            self.assertEqual(path.read_bytes(),original)

    def test_archive_rejects_invalid_headers_payload_and_metadata_atomically(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path,obj,data=self.archive(root);backup=root/'backup.bin'
            module.import_files(backup,'internal',[path]);before=backup.read_bytes()
            for field,value in [('version',2),('dataBase64','!invalid!'),('dataBase64',base64.b64encode(data[:-1]).decode()),('metadata',[]),('metadata',{**obj['metadata'],'dataSize':1}),('metadata',{**obj['metadata'],'filename':'../../bad'}),('metadata',{**obj['metadata'],'language':True}),('metadata',{**obj['metadata'],'comment':'x'*12}),('metadata',{**obj['metadata'],'saveDate':'bad'})]:
                with self.subTest(field=field,value=value):
                    path.write_text(json.dumps({**obj,field:value}))
                    with self.assertRaises(ValueError):module.import_files(backup,'internal',[path],True)
                    self.assertEqual(backup.read_bytes(),before)
            path.write_text(json.dumps({**obj,'dataBase64':base64.b64encode(data[:-1]+b'\xff').decode()}))
            with self.assertRaisesRegex(ValueError,'checksum'):module.raw_record(path)

    def test_scenario2_capacity_and_raw_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path,obj,data=self.archive(root);backup=root/'backup.bin'
            raw=root/'SFORCE32_04.BIN';raw.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'space'):module.import_files(backup,'internal',[path,raw])
            self.assertFalse(backup.exists())
            module.import_files(backup,'cartridge',[path,raw])
            self.assertEqual(len(module.records(Path(str(backup)+'.cartridge').read_bytes())),2)

    def fixture(self, root, slot):
        payload = bytes([slot]) + bytes(12895)
        path = root/f'SFORCE31_{slot:02d}.BIN'
        path.write_bytes((12896).to_bytes(4,'big') + sum(payload).to_bytes(4,'big') + payload)
        return path

    def test_preserves_payload_existing_records_and_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); a=self.fixture(root,1); b=self.fixture(root,2); backup=root/'backup.bin'
            module.import_files(backup,'internal',[a]); previous=backup.read_bytes()
            report=module.import_files(backup,'internal',[b]); entries=module.records(backup.read_bytes())
            self.assertEqual(entries[a.stem][32:],a.read_bytes())
            self.assertEqual(entries[b.stem][32:],b.read_bytes())
            self.assertEqual(Path(report['previous']).read_bytes(),previous)
            before=backup.read_bytes()
            with self.assertRaises(ValueError): module.import_files(backup,'internal',[b])
            self.assertEqual(backup.read_bytes(),before)

    def test_bad_checksum_and_full_internal_are_rejected_atomically(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); saves=[self.fixture(root,i) for i in (1,2,3)]; backup=root/'backup.bin'
            module.import_files(backup,'internal',saves[:2]); previous=backup.read_bytes()
            with self.assertRaises(ValueError): module.import_files(backup,'internal',[saves[2]])
            self.assertEqual(backup.read_bytes(),previous)
            module.import_files(backup,'cartridge',saves)
            self.assertEqual(len(module.records(Path(str(backup)+'.cartridge').read_bytes())),3)
            saves[2].write_bytes(saves[2].read_bytes()[:-1]+b'\xff')
            with self.assertRaises(ValueError): module.import_files(backup,'internal',[saves[2]])
            self.assertEqual(backup.read_bytes(),previous)


    def native_record(self, name, payload, comment=b'Test', language=1, date_minutes=0):
        head = name.encode().ljust(12, b'\0') + comment.ljust(11, b'\0') + bytes([language]) + struct.pack('<II', date_minutes, len(payload))
        return head + payload

    def bup_file(self, root, name, payload, comment=b'Test', language=1, date_minutes=0):
        path = root / (name + '.BUP')
        path.write_bytes(module.bup_bytes(self.native_record(name, payload, comment, language, date_minutes)))
        return path

    def test_bup_import_preserves_payload_and_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inner = b'\x03' + bytes(12895)
            payload = (12896).to_bytes(4, 'big') + sum(inner).to_bytes(4, 'big') + inner
            date_minutes = int((datetime.datetime(2026, 10, 20, 13, 52) - datetime.datetime(1980, 1, 1)).total_seconds() // 60)
            path = self.bup_file(root, 'SFORCE31_07', payload, comment=b'Julian', language=2, date_minutes=date_minutes)
            backup = root / 'backup.bin'
            module.import_files(backup, 'cartridge', [path])
            record = module.records(Path(str(backup) + '.cartridge').read_bytes())['SFORCE31_07']
            self.assertEqual(record[32:], payload)
            self.assertEqual(record[:12], b'SFORCE31_07\0')
            self.assertEqual(record[12:23], b'Julian\0\0\0\0\0')
            self.assertEqual(record[23], 2)
            self.assertEqual(struct.unpack_from('<II', record, 24), (date_minutes, len(payload)))

    def test_bup_export_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = (16240).to_bytes(4, 'big') + (7).to_bytes(4, 'big') + b'\x07' + bytes(16239)
            raw = root / 'SFORCE32_05.BIN'
            raw.write_bytes(data)
            backup = root / 'backup.bin'
            module.import_files(backup, 'cartridge', [raw])
            result = module.export_files(backup, 'cartridge', root / 'out', bup=True)
            exported = Path(result['exported'][0]['path'])
            self.assertTrue(exported.name.endswith('.BUP'))
            name, payload, metadata = module.bup_record(exported)
            self.assertEqual((name, payload), ('SFORCE32_05', data))
            again = root / 'again.bin'
            module.import_files(again, 'cartridge', [exported])
            self.assertEqual(module.records(Path(str(again) + '.cartridge').read_bytes())['SFORCE32_05'][32:], data)
            with self.assertRaisesRegex(ValueError, 'already exists'):
                module.export_files(backup, 'cartridge', root / 'out', bup=True)

    def test_bup_rejects_bad_magic_and_truncation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            good = self.bup_file(root, 'SFORCE33_01', bytes(24344))
            bad = root / 'bad.BUP'
            bad.write_bytes(b'XXXX' + good.read_bytes()[4:])
            with self.assertRaisesRegex(ValueError, 'Vmem BUP'):
                module.bup_record(bad)
            short = root / 'short.BUP'
            short.write_bytes(good.read_bytes()[:10])
            with self.assertRaisesRegex(ValueError, 'Truncated BUP'):
                module.bup_record(short)
            with self.assertRaises(ValueError):
                module.raw_record(bad)

    def test_bup_date_roundtrips_through_minutes(self):
        for stamp in (datetime.datetime(1980, 1, 1), datetime.datetime(1999, 12, 31, 23, 59),
                      datetime.datetime(2026, 10, 20, 13, 52), datetime.datetime(2107, 1, 1)):
            minutes = int((stamp - datetime.datetime(1980, 1, 1)).total_seconds() // 60)
            self.assertEqual(module.bup_date_to_minutes(module.minutes_to_bup_date(minutes)), minutes)


if __name__ == '__main__': unittest.main()
