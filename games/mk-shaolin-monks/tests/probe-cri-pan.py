"""Replay the retail IOP pan setter; hardware volume writes are out of scope."""
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('cri_queue_probe', HERE / 'probe-cri-queue.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def main():
    cpu = module.Iop(1, 3)
    # 0x2790 programs hardware volumes. Replace only that callee with JR RA/NOP;
    # the pan clamp and channel-indexed state store execute original instructions.
    cpu.put(0x2790, 0x03e00008)
    cpu.put(0x2794, 0)
    obj = 0x24000
    cases = []
    for channel in (0, 1):
        for value in (-2147483648, -16, -15, 0, 15, 16, 2147483647):
            cpu.put(obj + 0x40, 123)
            cpu.put(obj + 0x44, 123)
            cpu.call(0x29d0, [obj, channel, value & 0xffffffff])
            actual = module.signed(cpu.get(obj + 0x40 + 4 * channel))
            assert actual == max(-15, min(15, value))
            assert cpu.get(obj + 0x40 + 4 * (1 - channel)) == 123
            cases.append(dict(channel=channel, input=value, pan=actual))
    result = dict(irx_sha256=module.SHA, passed=True, cases=cases,
                  scope='Original 0x29d0 clamp/store; hardware-volume callee 0x2790 stubbed')
    (module.GAME / 'logs/cri-pan-probe.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: 14 original IOP pan cases, signed limits and per-channel isolation')

if __name__ == '__main__':
    main()
