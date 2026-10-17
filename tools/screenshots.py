#!/usr/bin/env python3
"""Pictures of every screen, taken from the modified unit over SWD, for the
documentation: one PNG a screen and an animated GIF of them all.

    python tools/screenshots.py            (from the project folder, ST-Link on the modified unit)

It writes the dso152-bl-shot build (build it first: pio run -e dso152-bl-shot),
which hands every rectangle it draws to this script (src/ScreenTap.cpp); the
script paints them into a picture of its own, so what is saved is the screen
pixel for pixel, without the screen being read. It then walks through the
menus by pulling the button pins low, the same way the test scripts do, with
the synthetic line of the demo build as data.

Nothing here is for an original unit: it stops if read protection is on.
The settings it leaves saved are the default theme, hex dump, blocks of 64.
At the end it writes the ordinary firmware (dso152-bl) back, since the build
it used does not run without this script.
"""
import glob, os, socket, struct, subprocess, sys, time, zlib

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD  = os.path.join(ROOT, '.pio', 'build', 'dso152-bl-shot')
OUTDIR = os.path.join(ROOT, 'docs', 'screens')
PIO    = os.path.join(os.path.expanduser('~'), '.platformio', 'packages')
OPENOCD = os.path.join(PIO, 'tool-openocd')
W, H, SCALE, FRAME_CS = 320, 240, 2, 200           # 200 hundredths: two seconds a frame

# buttons: (address of the port's reset register, of its set register, bit)
GPIOA, GPIOC = 0x40010800, 0x40011000
KEY = {'LEFT': (GPIOC, 3), 'OK': (GPIOC, 4), 'RIGHT': (GPIOC, 5), 'UP': (GPIOC, 6),
       'MODE': (GPIOC, 7), 'AUTO': (GPIOC, 8), 'DOWN': (GPIOA, 9), 'RUN': (GPIOA, 10)}


def symbols():
    nm = glob.glob(os.path.join(PIO, 'toolchain-gccarmnoneeabi*', 'bin', 'arm-none-eabi-nm.exe'))[0]
    out = subprocess.run([nm, '-C', os.path.join(BUILD, 'firmware.elf')], capture_output=True, text=True).stdout
    sym = {}
    for ln in out.splitlines():
        p = ln.split(' ', 2)
        if len(p) == 3:
            sym[p[2]] = int(p[0], 16)
    return sym


class Ocd:
    def __init__(self):
        self.proc = subprocess.Popen(
            [os.path.join(OPENOCD, 'bin', 'openocd.exe'), '-s', os.path.join(OPENOCD, 'openocd', 'scripts'),
             '-f', 'interface/stlink.cfg', '-c', 'transport select hla_swd', '-c', 'set CPUTAPID 0x2ba01477',
             '-f', 'target/stm32f1x.cfg', '-c', 'init'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                self.sock = socket.create_connection(('127.0.0.1', 6666), timeout=1)
                break
            except OSError:
                if self.proc.poll() is not None:
                    raise SystemExit('OpenOCD did not start: is the ST-Link connected?')
                time.sleep(0.2)
        else:
            raise SystemExit('no answer from OpenOCD')
        self.sock.settimeout(120)

    def cmd(self, text):
        self.sock.sendall(text.encode() + b'\x1a')
        buf = b''
        while not buf.endswith(b'\x1a'):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise SystemExit('OpenOCD went away')
            buf += chunk
        return buf[:-1].decode(errors='replace')

    def close(self):
        try:
            self.cmd('shutdown')
        except BaseException:
            pass
        try:
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


class Unit:
    def __init__(self, ocd, sym):
        self.o, self.sym = ocd, sym
        self.fb = [[0] * W for _ in range(H)]
        self.tail = 0
        self.rects = 0
        n = 192
        ocd.cmd('proc tapdrain {tail} { set h [read_memory %d 32 1]; if {$h == $tail} { return $h }; '
                'if {$h > $tail} { set d [read_memory [expr {%d + 8*$tail}] 32 [expr {2*($h-$tail)}]] } else { '
                'set d [read_memory [expr {%d + 8*$tail}] 32 [expr {2*(%d-$tail)}]]; '
                'if {$h > 0} { set d "$d [read_memory %d 32 [expr {2*$h}]]" } }; mww %d $h; return "$h $d" }'
                % (sym['tapHead'], sym['tapBuf'], sym['tapBuf'], n, sym['tapBuf'], sym['tapTail']))

    def byte(self, name):
        return int(self.o.cmd('read_memory %d 8 1' % self.sym[name]).split()[0], 0)

    def word(self, name):
        return int(self.o.cmd('read_memory %d 32 1' % self.sym[name]).split()[0], 0)

    def poke(self, name, v):
        self.o.cmd('mwb %d %d' % (self.sym[name], v))

    def drain(self):
        """Takes what the unit has drawn since last time. Returns how many rectangles."""
        v = [int(x, 0) for x in self.o.cmd('tapdrain %d' % self.tail).split()]
        self.tail = v[0]
        d = v[1:]
        for i in range(0, len(d) - 1, 2):
            a, b = d[i], d[i + 1]
            x, y, w = a & 0x1FF, (a >> 9) & 0xFF, ((a >> 17) & 0x1FF) + 1
            h, c = (b & 0xFF) + 1, b >> 16
            fill = [c] * w
            for yy in range(y, min(y + h, H)):
                self.fb[yy][x:x + w] = fill
        self.rects += len(d) // 2
        return len(d) // 2

    def run_for(self, seconds):
        t0 = time.time()
        while time.time() - t0 < seconds:
            if not self.drain():
                time.sleep(0.01)

    def settle(self, quiet=0.6, limit=60):
        t0 = last = time.time()
        while time.time() - last < quiet and time.time() - t0 < limit:
            if self.drain():
                last = time.time()
            else:
                time.sleep(0.01)

    def wait(self, cond, limit, what):
        t0 = time.time()
        while not cond():
            if time.time() - t0 > limit:
                raise SystemExit('gave up waiting for ' + what)
            self.drain()
        return time.time() - t0

    def press(self, key, hold=0.22):
        port, bit = KEY[key]
        self.o.cmd('mww %d %d' % (port + 0x14, 1 << bit))      # BRR: pulled low, the key is down
        self.run_for(hold)
        self.o.cmd('mww %d %d' % (port + 0x10, 1 << bit))      # BSRR: up again
        self.run_for(0.3)


# --- pictures ------------------------------------------------------------------
def rgb(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def write_png(path, fb):
    raw = bytearray()
    for row in fb:
        line = bytearray()
        for c in row:
            line += bytes(rgb(c)) * SCALE
        raw += (b'\x00' + line) * SCALE
    def chunk(tag, d):
        return struct.pack('>I', len(d)) + tag + d + struct.pack('>I', zlib.crc32(tag + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W * SCALE, H * SCALE, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def lzw(indices, min_bits):
    clear, end = 1 << min_bits, (1 << min_bits) + 1
    out, acc, nacc = bytearray(), 0, 0
    size, nxt, table = min_bits + 1, end + 1, {}

    def emit(code):
        nonlocal acc, nacc
        acc |= code << nacc
        nacc += size
        while nacc >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nacc -= 8

    emit(clear)
    prefix = indices[0]
    for k in indices[1:]:
        key = (prefix << 8) | k
        code = table.get(key)
        if code is not None:
            prefix = code
            continue
        emit(prefix)
        if nxt < 4096:
            table[key] = nxt
            nxt += 1
            if nxt > (1 << size) and size < 12:
                size += 1
        else:
            emit(clear)
            table, nxt, size = {}, end + 1, min_bits + 1
        prefix = k
    emit(prefix)
    emit(end)
    if nacc:
        out.append(acc & 0xFF)
    return bytes(out)


def write_gif(path, frames):
    shift = 0
    while True:                                     # if there are too many colours, tell fewer apart
        colours = sorted({c >> shift << shift for fb in frames for row in fb for c in row})
        if len(colours) <= 256:
            break
        shift += 1
    index = {c: i for i, c in enumerate(colours)}
    pal = b''.join(bytes(rgb(c)) for c in colours).ljust(768, b'\x00')
    w, h = W * SCALE, H * SCALE
    with open(path, 'wb') as f:
        f.write(b'GIF89a' + struct.pack('<HHBBB', w, h, 0xF7, 0, 0) + pal)
        f.write(b'\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00')            # repeat for ever
        for fb in frames:
            idx = bytearray()
            for row in fb:
                line = bytearray()
                for c in row:
                    line += bytes([index[c >> shift << shift]]) * SCALE
                idx += line * SCALE
            f.write(b'\x21\xF9\x04\x00' + struct.pack('<H', FRAME_CS) + b'\x00\x00')
            f.write(b'\x2C' + struct.pack('<HHHHB', 0, 0, w, h, 0) + b'\x08')
            data = lzw(idx, 8)
            for i in range(0, len(data), 255):
                f.write(bytes([len(data[i:i + 255])]) + data[i:i + 255])
            f.write(b'\x00')
        f.write(b'\x3B')
    return len(colours)


# --- the walk ----------------------------------------------------------------------
def main():
    binfile = os.path.join(BUILD, 'firmware.bin')
    if not os.path.exists(binfile):
        raise SystemExit('build it first: pio run -e dso152-bl-shot')
    os.makedirs(OUTDIR, exist_ok=True)
    for old in glob.glob(os.path.join(OUTDIR, '*.png')):   # the numbers shift when the walk changes
        os.remove(old)
    sym = symbols()
    ocd = Ocd()
    frames, names = [], []
    try:
        ocd.cmd('reset halt')
        if int(ocd.cmd('read_memory 0x4002201c 32 1').split()[0], 0) & 2:
            raise SystemExit('read protection is on: this is an original unit, nothing was written')
        print('writing', os.path.basename(BUILD), flush=True)
        r = ocd.cmd('flash write_image erase {%s} 0x08003C00 bin' % binfile.replace('\\', '/'))
        if 'wrote' not in r:
            raise SystemExit('flash failed: ' + r)
        ocd.cmd('reset run')
        u = Unit(ocd, sym)

        def shot(name):
            u.settle()
            if frames and u.fb == frames[-1]:
                print('     %s: the same as the one before, left out' % name, flush=True)
                return
            frames.append([row[:] for row in u.fb])
            names.append(name)
            write_png(os.path.join(OUTDIR, '%02d_%s.png' % (len(frames), name)), u.fb)
            print('  %02d %s' % (len(frames), name), flush=True)

        u.run_for(3)
        u.poke('Theme::idx', 0); u.poke('Config::format', 1); u.poke('Config::block', 1)
        shot('menu')

        u.press('OK')
        t = u.wait(lambda: u.word('Rs232::g_bytes') >= 38, 400, 'two messages on the synthetic line')
        print('  (two messages after %.0f s)' % t, flush=True)
        u.run_for(4)                                        # into the rest after the message
        # While it runs, a turn of the inspector's loop takes as long as a repaint
        # does through the tap, and a key is looked at once a turn: hold it until
        # it has been seen.
        for attempt in range(5):
            u.press('RUN', hold=1.5)                        # HOLD
            if u.byte('running') == 0:
                break
        else:
            raise SystemExit('RUN was not taken: the inspector is still running')
        shot('tester_protocol')
        for name in ('tester_teach', 'tester_large', 'tester_hex', 'tester_chars'):
            u.press('MODE', hold=0.45)                      # under 0.7 s, or it counts as held
            shot(name)
        u.press('MODE', hold=0.45)                          # back to the protocol view
        u.settle()
        for attempt in range(4):                            # SNAP: the first one may begin mid message
            u.press('RUN')
            u.wait(lambda: u.byte('snap') == 4, 200, 'the capture')
            u.settle()
            if u.byte('Rs232::v_byteN') == 8:
                break
        shot('tester_snap')
        # The same capture in each of the other themes. The theme is set from
        # here, and the view set to the last one in the round, so that one
        # press of MODE comes back to the protocol view and draws everything
        # again in the new colours; nothing is written to flash for this.
        def redraw_in(theme_idx):
            u.poke('Theme::idx', theme_idx)
            u.poke('displayMode', 2)                        # characters: the last before the protocol view
            for _ in range(3):
                u.press('MODE', hold=0.45)
                u.settle(quiet=0.35, limit=30)
                if u.byte('displayMode') == 0: break
        for i, theme in enumerate(('light', 'eink', 'night', 'phosphor', 'bluesky', 'orange'), 1):
            redraw_in(i)
            shot('tester_snap_' + theme)
        redraw_in(0)
        u.settle()
        u.press('OK', hold=1.0)                             # held: the menu
        u.settle()

        u.press('DOWN'); u.press('OK')
        shot('generator_off')
        u.press('RUN')
        shot('generator_1khz')
        for _ in range(4):
            u.press('LEFT')
        shot('generator_3v3')
        u.press('MODE')
        u.settle()

        u.press('DOWN'); u.press('OK')
        shot('sender_all')
        u.press('DOWN')
        shot('sender_block')
        u.press('MODE')
        u.settle()

        u.press('DOWN'); u.press('OK')
        shot('settings')
        for i in range(1, 7):
            u.press('RIGHT')
            shot('theme_%d' % i)
        u.press('RIGHT')                                    # round to the default again
        u.settle()
        u.press('DOWN')
        shot('settings_export')
        u.press('DOWN'); u.press('DOWN'); u.press('RIGHT')
        shot('flash_page')
        u.press('OK')
        u.settle()
        u.press('MODE')
        shot('menu_end')
        frames.pop(); names.pop()                           # the same as the first; kept only as a PNG
        os.remove(os.path.join(OUTDIR, '%02d_menu_end.png' % (len(frames) + 1)))
        print('  %d rectangles in all' % u.rects)

        normal = os.path.join(ROOT, '.pio', 'build', 'dso152-bl', 'firmware.bin')
        if os.path.exists(normal):
            ocd.cmd('reset halt')
            r = ocd.cmd('flash write_image erase {%s} 0x08003C00 bin' % normal.replace('\\', '/'))
            ocd.cmd('reset run')
            print('the ordinary firmware is back on the unit' if 'wrote' in r else 'COULD NOT write dso152-bl back: ' + r)
        else:
            print('dso152-bl is not built: the unit is left with the screenshot build, which needs this script')
    finally:
        ocd.close()

    gif = os.path.join(ROOT, 'docs', 'screens.gif')
    n = write_gif(gif, frames)
    print('%d frames, %d colours, %d kB -> %s' % (len(frames), n, os.path.getsize(gif) // 1024, gif))


if __name__ == '__main__':
    main()
