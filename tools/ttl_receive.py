#!/usr/bin/env python3
"""Takes what the DSO-152 TTL Sender puts on the calibration output and turns
it back into the bytes that were captured, checking it on the way.

    python ttl_receive.py --port COM5 --baud 9600        from a USB-TTL adapter
    python ttl_receive.py capture.log                    from a saved terminal log

Adapter RX to the calibration output, ground to ground. With --port, start
this first, then press RUN on the instrument; it stops by itself once the
line has been quiet for --idle seconds.

The format is recognised from the data: the hex dump (lines 'offset: hex
text', with '# ...' lines carrying block numbers and CRC-32), Intel HEX
(lines beginning ':'), or failing both, raw bytes. The bytes are written to
--out (default received.bin) and their CRC-32 is printed: it must be the one
the instrument shows beside the byte count.

Needs pyserial only for --port:  pip install pyserial
"""
import argparse, re, sys, time, zlib


def read_port(port, baud, idle):
    import serial
    data = bytearray()
    with serial.Serial(port, baud, timeout=0.2) as s:
        print(f'listening on {port} at {baud} 8N1; press RUN on the instrument', file=sys.stderr)
        last = None
        while True:
            chunk = s.read(4096)
            if chunk:
                data += chunk
                last = time.time()
            elif last is not None and time.time() - last > idle:
                break
    return bytes(data)


DUMP_LINE = re.compile(r'^([0-9a-fA-F]{8}): ((?:[0-9a-fA-F]{2,4} ?)+)')
BLOCK_HDR = re.compile(r'^# DSO152 block (\d+)/(\d+) offset (\d+) length (\d+)')
BLOCK_CRC = re.compile(r'^# crc32 ([0-9A-Fa-f]{8})')
TOTAL     = re.compile(r'^# total (\d+) bytes crc32 ([0-9A-Fa-f]{8})')


def parse_dump(lines):
    """Returns (bytes by offset, problems, notes)."""
    mem, problems, notes = {}, [], []
    cur = None                      # (blk, nblk, off, length, data so far)
    total = None
    for ln in lines:
        m = BLOCK_HDR.match(ln)
        if m:
            cur = [int(m[1]), int(m[2]), int(m[3]), int(m[4]), bytearray()]
            continue
        m = DUMP_LINE.match(ln)
        if m:
            off = int(m[1], 16)
            hexpart = ln[10:50] if len(ln) >= 50 else m[2]
            b = bytes.fromhex(hexpart.replace(' ', ''))
            for i, v in enumerate(b):
                mem[off + i] = v
            if cur is not None:
                cur[4] += b
            continue
        m = BLOCK_CRC.match(ln)
        if m and cur is not None:
            want, got = int(m[1], 16), zlib.crc32(bytes(cur[4])) & 0xFFFFFFFF
            ok = want == got and len(cur[4]) == cur[3]
            notes.append(f'block {cur[0]}/{cur[1]} at {cur[2]}: {len(cur[4])} bytes, crc32 {got:08X} '
                         + ('ok' if ok else f'BAD (sent {want:08X}, {cur[3]} bytes)'))
            if not ok:
                problems.append(f'block {cur[0]} is damaged: send it again')
            cur = None
            continue
        m = TOTAL.match(ln)
        if m:
            total = (int(m[1]), int(m[2], 16))
    if cur is not None:
        problems.append(f'block {cur[0]} has no crc line: it was cut short')
    return mem, problems, notes, total


def parse_ihex(lines):
    mem, problems, notes = {}, [], []
    ended = False
    for n, ln in enumerate(lines, 1):
        if not ln.startswith(':'):
            continue
        try:
            rec = bytes.fromhex(ln[1:].strip())
        except ValueError:
            problems.append(f'line {n}: not hex')
            continue
        if len(rec) < 5 or len(rec) != rec[0] + 5 or sum(rec) & 0xFF:
            problems.append(f'line {n}: bad length or checksum')
            continue
        count, addr, typ = rec[0], (rec[1] << 8) | rec[2], rec[3]
        if typ == 0:
            for i in range(count):
                mem[addr + i] = rec[4 + i]
        elif typ == 1:
            ended = True
    if not ended:
        problems.append('no end record: the export was cut short')
    return mem, problems, notes, None


def assemble(mem, problems):
    if not mem:
        return b''
    lo, hi = min(mem), max(mem)
    missing = [a for a in range(lo, hi + 1) if a not in mem]
    if missing:
        problems.append(f'{len(missing)} bytes missing between offsets {lo} and {hi}, first at {missing[0]}')
    if lo != 0:
        problems.append(f'starts at offset {lo}, not 0: this is part of the buffer only')
    return bytes(mem.get(a, 0) for a in range(lo, hi + 1))


def hexdump(b):
    for i in range(0, len(b), 16):
        row = b[i:i + 16]
        print(f'{i:08x}: {row.hex(" "):<47}  ' + ''.join(chr(c) if 32 <= c < 127 else '.' for c in row))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('file', nargs='?', help='a saved capture, instead of --port')
    ap.add_argument('--port', help='serial port of the USB-TTL adapter, e.g. COM5')
    ap.add_argument('--baud', type=int, default=9600)
    ap.add_argument('--idle', type=float, default=2.0, help='seconds of silence that end a capture')
    ap.add_argument('--out', default='received.bin')
    ap.add_argument('--quiet', action='store_true', help='do not print the bytes')
    a = ap.parse_args()

    if a.port:
        raw = read_port(a.port, a.baud, a.idle)
    elif a.file:
        raw = open(a.file, 'rb').read()
    else:
        ap.error('give a file or --port')

    text = raw.decode('latin-1')
    lines = [ln.rstrip('\r') for ln in text.split('\n')]
    problems, notes, total = [], [], None
    if any(DUMP_LINE.match(ln) for ln in lines):
        kind = 'hex dump'
        mem, problems, notes, total = parse_dump(lines)
        data = assemble(mem, problems)
    elif any(ln.startswith(':') for ln in lines):
        kind = 'Intel HEX'
        mem, problems, notes, total = parse_ihex(lines)
        data = assemble(mem, problems)
    else:
        kind = 'raw'
        data = raw

    crc = zlib.crc32(data) & 0xFFFFFFFF
    if total is not None and (total[0] != len(data) or total[1] != crc):
        problems.append(f'the instrument sent {total[0]} bytes with crc32 {total[1]:08X}')

    print(f'format: {kind}   received: {len(raw)} characters')
    for n in notes:
        print('  ' + n)
    print(f'{len(data)} bytes   CRC {crc:08X}   <- compare with the instrument\'s screen')
    if total is not None and not problems:
        print('the total line of the export agrees')
    if not a.quiet:
        hexdump(data)
    open(a.out, 'wb').write(data)
    print(f'written to {a.out}')
    for p in problems:
        print('PROBLEM: ' + p)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
