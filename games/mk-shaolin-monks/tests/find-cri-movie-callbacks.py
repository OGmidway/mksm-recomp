"""Audit the retail CRI module registry used by 0x451f48/0x451ff8.

This only finds candidates. RecoverMKSMEntries validates function boundaries;
generate-supplement validates that every selected entry was emitted.
"""
import importlib.util
import json
import pathlib
import struct

GAME = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('symbols', GAME/'tests/transfer-prototype-symbols.py')
symbols = importlib.util.module_from_spec(spec)
spec.loader.exec_module(symbols)
read = symbols.image(symbols.ROOT/'MortalKombatShaolinMonks/SLUS_210.87', symbols.HASHES['retail'])
registry = struct.unpack('<15I', read(0x5ab770, 60))
assert registry[:7] == (0x5ad240,0x5ad278,0x5ad2c0,0x5ad7a0,0x5ad008,0x5ad060,0x5ad768)
assert not any(registry[7:])
tables = []
for table in registry[:7]:
    entries = struct.unpack('<14I', read(table, 56))
    assert all(0x100000 <= address < 0x49b000 and address % 4 == 0 for address in entries)
    tables.append({'table':hex(table), 'entries':[hex(address) for address in entries]})
report = {'retail_sha256':symbols.HASHES['retail'], 'registry':'0x5ab770',
          'evidence':'Retail data registry; 0x451f48 copies 15 table pointers; 0x451ff8 dispatches a slot across these modules.',
          'tables':tables, 'callbacks':sorted({entry for table in tables for entry in table['entries']}, key=lambda x:int(x,16))}
(GAME/'logs/cri-movie-callback-recovery.json').write_text(json.dumps(report,indent=2)+'\n')
print(f"Verified 7 retail tables; {len(report['callbacks'])} callback candidates")
