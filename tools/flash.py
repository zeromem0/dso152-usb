#!/usr/bin/env python3
"""Writes a build to the modified unit over SWD, at 0x08003C00, after making
sure it is not an original unit (read protection on). Then starts it and
presses OK so that the USB serial screen comes up.

    python tools/flash.py [env]        (default dso152-bl)
"""
import os, sys, time
import screenshots as sh

env = sys.argv[1] if len(sys.argv) > 1 else 'dso152-bl'
image = os.path.join(sh.ROOT, '.pio', 'build', env, 'firmware.bin').replace(os.sep, '/')
ocd = sh.Ocd()
try:
    ocd.cmd('reset halt')
    if int(ocd.cmd('read_memory 0x4002201c 32 1').split()[0], 0) & 2:
        raise SystemExit('read protection is on: this is an original unit, nothing was written')
    r = ocd.cmd('flash write_image erase {%s} 0x08003C00 bin' % image)
    if 'wrote' not in r:
        raise SystemExit('flash failed: ' + r)
    print('verify:', 'ok' if 'verified' in ocd.cmd('verify_image {%s} 0x08003C00 bin' % image) else 'FAILED')
    ocd.cmd('reset run')
    time.sleep(3)
    port, bit = sh.KEY['OK']
    ocd.cmd('mww %d %d' % (port + 0x14, 1 << bit)); time.sleep(0.3)
    ocd.cmd('mww %d %d' % (port + 0x10, 1 << bit)); time.sleep(0.5)
    cfgr = int(ocd.cmd('read_memory 0x40021004 32 1').split()[0], 0)
    print('running %s; clock from %s, PLL x%d' % (env, 'HSE' if cfgr >> 16 & 1 else 'HSI/2', ((cfgr >> 18) & 15) + 2))
finally:
    ocd.close()
