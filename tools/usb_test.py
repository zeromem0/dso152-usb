#!/usr/bin/env python3
"""USB serial on the modified unit, after tools/flash.py: both directions apart
(4 kB each, the instrument counting what came), then 4 kB through the echo,
then two buttons pressed over SWD, which must arrive as lines on the PC.

    python tools/usb_test.py [COMx]     (default: the port with VID 0483 PID 5740)
"""
import serial, serial.tools.list_ports, time, sys, os, random
import screenshots as sh

sh.BUILD = os.path.join(sh.ROOT, '.pio', 'build', 'dso152-bl')
port = sys.argv[1] if len(sys.argv) > 1 else next(
    (p.device for p in serial.tools.list_ports.comports() if p.vid == 0x0483 and p.pid == 0x5740), None)
if not port:
    raise SystemExit('no DSO-152 USB serial port found')
sym = sh.symbols()
ocd = sh.Ocd()
ok = True
def rd(n, w=32): return int(ocd.cmd('read_memory %d %d 1' % (sym[n], w)).split()[0], 0)
def check(what, good):
    global ok
    ok &= bool(good)
    print('  %-60s %s' % (what, 'OK' if good else 'WRONG'))
def key(k):
    p_, b = sh.KEY[k]
    ocd.cmd('mww %d %d' % (p_ + 0x14, 1 << b)); time.sleep(0.3)
    ocd.cmd('mww %d %d' % (p_ + 0x10, 1 << b)); time.sleep(0.5)
try:
    p = serial.Serial(port, 115200, timeout=0.2, write_timeout=5)
    p.dtr = True
    time.sleep(0.8)
    banner = p.read(p.in_waiting)
    check('%s: greeting %r' % (port, banner[:40]), banner.startswith(b'DSO-152'))

    ocd.cmd('mwb %d 0' % sym['g_echo'])
    in0 = rd('g_in'); t0 = time.time()
    p.write(b'x' * 4096); p.flush()
    time.sleep(1)
    n = rd('g_in') - in0
    check('PC -> instrument: %d of 4096 (%.2f s)' % (n, time.time() - t0 - 1), n == 4096)

    out0 = rd('g_out'); t0 = time.time()
    ocd.cmd('mwh %d 4096' % sym['g_burst'])
    got = b''
    while len(got) < 4096 and time.time() - t0 < 6:
        got += p.read(max(1, p.in_waiting))
    check('instrument -> PC: %d of 4096 (%.2f s)' % (len(got), time.time() - t0), len(got) == 4096)
    ocd.cmd('mwb %d 1' % sym['g_echo'])

    random.seed(1)
    data = bytes(random.choice(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ') for _ in range(4096))
    t0 = time.time(); got = b''
    for i in range(0, len(data), 256):
        p.write(data[i:i + 256])
        got += p.read(p.in_waiting or 1)
    while len(got) < len(data) and time.time() - t0 < 10:
        got += p.read(max(1, p.in_waiting))
    check('echo: %d of 4096 back, identical (%.2f s)' % (len(got), time.time() - t0), got == data)

    # After 4 kB of text the terminal repaints all its rows, which takes longer
    # than a press; a press during it would not be seen.
    time.sleep(1.5)
    p.read(p.in_waiting)
    for k, want in (('UP', b'key UP\r\n'), ('RUN', b'test line'), ('LEFT', b'key LEFT\r\n')):
        key(k)
        line = p.read(p.in_waiting)
        check('button %s: %r' % (k, line), line.startswith(want))
    p.close()
finally:
    ocd.close()
print('  ->', 'OK' if ok else 'WRONG')
