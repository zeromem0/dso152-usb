#!/usr/bin/env python3
"""Writes the FNIRSI bootloader copied from the original unit
(dump/bootloader_original.bin) to 0x08000000 on the modified unit, in place of
the 28-byte shim, after saving what is there; only those 15 pages are erased.
Then starts the unit and checks that the bootloader hands over to the
application at 0x08003C00.

    python tools/put_bootloader.py
"""
import os, time, zlib
import screenshots as sh

DUMP = os.path.join(sh.ROOT, 'dump')
BL = os.path.join(DUMP, 'bootloader_original.bin')
sh.BUILD = os.path.join(sh.ROOT, '.pio', 'build', 'dso152-bl')

data = open(BL, 'rb').read()
assert len(data) == 0x3C00 and zlib.crc32(data) == 0x5A4BE94E, 'not the copy that was checked'
sym = sh.symbols()
ocd = sh.Ocd()
def rd(a, w=32): return int(ocd.cmd('read_memory %d %d 1' % (a, w)).split()[0], 0)
try:
    ocd.cmd('reset halt')
    if rd(0x4002201c) & 2:
        raise SystemExit('read protection is on: this is an original unit, nothing was written')
    before = bytes(int(v, 0) for v in ocd.cmd('read_memory 0x08000000 8 %d' % 0x3C00).split())
    keep = os.path.join(DUMP, 'modified_unit_0x08000000_before_bootloader.bin')
    if not os.path.exists(keep):
        open(keep, 'wb').write(before)
    print('saved what was at 0x08000000: %s (first bytes %s)' % (os.path.basename(keep), before[:8].hex(' ')))
    app0 = zlib.crc32(bytes(int(v, 0) for v in ocd.cmd('read_memory 0x08003C00 8 1024').split()))

    r = ocd.cmd('flash write_image erase {%s} 0x08000000 bin' % BL.replace(os.sep, '/'))
    print(r.strip().splitlines()[-1] if r.strip() else r)
    v = ocd.cmd('verify_image {%s} 0x08000000 bin' % BL.replace(os.sep, '/'))
    print('verify:', 'ok' if 'verified' in v else 'FAILED ' + v)
    app1 = zlib.crc32(bytes(int(v, 0) for v in ocd.cmd('read_memory 0x08003C00 8 1024').split()))
    print('application at 0x08003C00 untouched:', app0 == app1)

    ocd.cmd('reset run')
    time.sleep(4)
    t0 = rd(sym['uwTick']); time.sleep(1); t1 = rd(sym['uwTick'])
    pc = int(ocd.cmd('halt; reg pc').split(':')[-1].split()[0], 0); ocd.cmd('resume')
    print('after reset: pc %08x (%s), the application counts time: %d ms in 1 s, menu app=%d'
          % (pc, 'application' if pc >= 0x08003C00 else 'bootloader', t1 - t0, rd(sym['app'], 8)))
    print('VTOR %08x' % rd(0xE000ED08))
finally:
    ocd.close()
