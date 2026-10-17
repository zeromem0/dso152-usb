#!/usr/bin/env python3
"""The USB Sender on the modified unit, with the ordinary firmware: for two
stretches of flash (the calibration page, 1 kB, and the bootloader area,
15 kB), Settings is set to send it, the sender is started with RUN, what
comes out of the USB serial port is taken on this side, turned back into
bytes by tools/ttl_receive.py, and compared with the same flash read over SWD.
Then the screen of Memory is opened and left, which must not stop anything.

    python tools/sender_test.py         (after: pio run -e dso152-bl)
"""
import os, sys, time, subprocess, zlib, threading
import serial, serial.tools.list_ports
import screenshots as sh

sh.BUILD = os.path.join(sh.ROOT, '.pio', 'build', 'dso152-bl')
HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def main():
    image = os.path.join(sh.BUILD, 'firmware.bin').replace(os.sep, '/')
    sym = sh.symbols()
    ocd = sh.Ocd()
    ok = True

    def check(what, good):
        nonlocal ok
        ok &= bool(good)
        print('  %-62s %s' % (what, 'OK' if good else 'WRONG'), flush=True)
    def byte(n): return int(ocd.cmd('read_memory %d 8 1' % sym[n]).split()[0], 0)
    def key(k, hold=0.3):
        port, bit = sh.KEY[k]
        ocd.cmd('mww %d %d' % (port + 0x14, 1 << bit)); time.sleep(hold)
        ocd.cmd('mww %d %d' % (port + 0x10, 1 << bit)); time.sleep(0.5)
    def flash(addr, n):
        out = b''
        for a in range(addr, addr + n, 1024):
            out += bytes(int(v, 0) for v in ocd.cmd('read_memory %d 8 %d' % (a, min(1024, addr + n - a))).split())
        return out

    try:
        ocd.cmd('reset halt')
        if int(ocd.cmd('read_memory 0x4002201c 32 1').split()[0], 0) & 2:
            raise SystemExit('read protection is on: this is an original unit, nothing was written')
        r = ocd.cmd('flash write_image erase {%s} 0x08003C00 bin' % image)
        if 'wrote' not in r:
            raise SystemExit('flash failed: ' + r)
        ocd.cmd('reset run')
        time.sleep(4)
        port = next((p.device for p in serial.tools.list_ports.comports()
                     if p.vid == 0x0483 and p.pid == 0x5740), None)
        check('the USB serial port is there: %s' % port, port)

        def toSource():
            # the row Settings was left on is kept: move to Source (3) from wherever
            for _ in range(6):
                r = byte('Settings::g_row')
                if r == 3: break
                key('DOWN' if r < 3 else 'UP')
            return byte('Settings::g_row') == 3

        # Settings, the Source row
        for _ in range(4): key('DOWN')
        key('OK')
        check('menu, OK: Settings', byte('app') == 2)
        check('the Source row', toSource())

        for name, src, base, size in (('Calibration', 4, 0x0800FC00, 1024), ('Bootloader', 1, 0x08000000, 0x3C00)):
            for _ in range(6):
                if byte('Config::source') == src: break
                key('RIGHT')
            check('Settings: source %d, %s' % (src, name), byte('Config::source') == src)
            key('MODE')
            key('UP'); key('UP')
            for _ in range(3): key('DOWN')
            key('UP'); key('UP')                  # Settings is the last: up twice is the sender
            key('OK')
            check('the sender', byte('app') == 3)

            p = serial.Serial(port, 115200, timeout=0.02)
            p.dtr = True
            time.sleep(0.5); p.read(p.in_waiting)
            # read all along, as a real receiver does, also while RUN is being pressed
            got_parts, last = [], [time.time()]
            stop = threading.Event()
            def reader():
                while not stop.is_set():
                    chunk = p.read(max(1, p.in_waiting))
                    if chunk: got_parts.append(chunk); last[0] = time.time()
            th = threading.Thread(target=reader); th.start()
            t0 = time.time()
            key('RUN')
            while time.time() - last[0] < 1.5 and time.time() - t0 < 60:
                time.sleep(0.05)
            stop.set(); th.join()
            log = b''.join(got_parts)
            dt = time.time() - t0
            p.close()
            path = os.path.join(HERE, '_%s.log' % name.lower())
            open(path, 'wb').write(log)
            out = os.path.join(HERE, '_%s.bin' % name.lower())
            rep = subprocess.run([PY, os.path.join(HERE, 'ttl_receive.py'), path, '--out', out, '--quiet'],
                                 capture_output=True, text=True).stdout
            got = open(out, 'rb').read() if os.path.exists(out) else b''
            want = flash(base, size)
            ms = int(ocd.cmd('read_memory %d 32 1' % sym['Ttl::g_sendMs']).split()[0], 0)
            print('    the instrument took %d ms (%.1f kB/s)' % (ms, len(log) / 1024 / max(ms, 1) * 1000))
            print('    %d characters in %.1f s; receiver: %s' % (len(log), dt, rep.strip().splitlines()[-2] if rep.strip() else '?'))
            check('%s: %d bytes back, identical to flash, CRC %08X' % (name, len(got), zlib.crc32(want)), got == want)
            check('no problem reported by the receiver', 'PROBLEM' not in rep)
            for f in (path, out):
                if os.path.exists(f): os.remove(f)

            key('MODE')                           # back to the menu, then to Settings again
            for _ in range(3): key('DOWN')
            key('OK')
            toSource()
        key('MODE')

        # Memory opens and leaves
        key('UP')
        key('OK')
        check('Memory', byte('app') == 4)
        key('AUTO')
        check('AUTO: the next area', byte('Memory::g_area') == 2)
        key('MODE')
        check('MODE: the menu', byte('app') == 0)
    finally:
        ocd.close()
    print('  ->', 'OK' if ok else 'WRONG')


if __name__ == '__main__':
    main()
