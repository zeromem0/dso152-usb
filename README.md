# USB serial port for the Fnirsi DSO152

Alternative firmware for the FNIRSI DSO-152 that turns the instrument's own USB socket into a
serial port (USB CDC, a virtual COM port), with a terminal, a sender that exports data and
flash contents to the PC in formats checked with a CRC-32, and a flash viewer.

> **Note:** the initial work comes from David's
> [dso152-breakout](https://github.com/davidgiven/dso152-breakout).

## What it does

- **USB Serial:** a terminal. What the PC sends is shown on the screen and echoed back;
  the buttons send lines to the PC.
- **USB Sender:** sends what the terminal received, or a stretch of flash (Bootloader,
  Application, Settings, Calibration), to the PC at about 370 kB/s, as raw bytes, a hex dump
  with a CRC-32 per block, or Intel HEX. The instrument's screen shows the CRC-32 of what it
  sends, to be compared with the one computed on the PC.
- **Memory:** shows the flash in hexadecimal or as characters.
- **Settings:** colour theme, export format, block size, source; kept in flash.

On Windows 10 and later the instrument appears as *USB Serial Device (COMx)*, with the driver
included in Windows. Nothing needs to be installed.

## Ready-made binary

[`usb/USB_v0.94.bin`](usb/) is a build of this firmware. It can be installed as it is, see
[Installing](#installing-through-the-usb-drive).

## Building

You need [PlatformIO](https://platformio.org/): the VS Code extension, or the command line
(`pip install platformio`). The platform (`ststm32@19.0.0`) and the library (Adafruit GFX
1.12.6) are pinned in `platformio.ini` and are downloaded on the first build.

```
git clone https://github.com/zeromem0/dso152-usb.git
cd dso152-usb
pio run -e dso152-bl
```

The result is `.pio/build/dso152-bl/firmware.bin`.

**Build `dso152-bl`, not the default environment.** `dso152-bl` is linked at `0x08003C00`,
above the FNIRSI bootloader, where the bootloader expects the application. The default
`dso152` is linked at `0x08000000` and is only for a unit flashed over SWD without a
bootloader. `dso152-bl-nousb` is the same firmware without USB, kept to measure what USB
costs; `dso152-bl-shot` is a test build that needs a debugger attached. **Do not install
these on a unit.**

## Installing through the USB drive

The DSO-152 has a bootloader that shows up as a USB drive. Nothing needs to be opened, and
the bootloader itself is never touched.

1. Rename the binary to exactly **`DSO152_APP_V0.1.bin`**.
2. Switch the unit off.
3. Connect it to the computer with a USB **data** cable. A charge-only cable is not enough.
4. **Hold OK and switch it on.** The screen shows *Enter firmware upgrade mode*, and a USB
   drive named *DSO BOOT* appears on the computer.
5. Copy `DSO152_APP_V0.1.bin` onto that drive. The drive disappears once the bootloader has
   taken the file.
6. The unit does not start the new firmware by itself: **press the power button briefly**,
   and the start menu appears.

Good to know:

- With this firmware installed, the text on the upgrade screen is garbled. The bootloader
  takes its font from the application's area of flash. It is harmless: the drive still works.
- **Going back to the oscilloscope:** install the FNIRSI firmware file the same way.
- The FNIRSI calibration page (`0x0800FC00`) is never written. Settings are kept in the page
  below it (`0x0800F800`).

## Receiving an export on the PC

Choose the source and the format in Settings, open **USB Sender**, then on the PC run:

```
powershell -ExecutionPolicy Bypass -File tools\usb_receive.ps1 -ListPorts
powershell -ExecutionPolicy Bypass -File tools\usb_receive.ps1 -Port COM12
```

and press RUN on the instrument. The script stops two seconds after the data ends, writes
`received.bin`, checks the CRC-32 of every block, and prints the CRC-32 of the whole; it must
match the one on the instrument's screen. The speed given with `-Baud` is ignored over USB.
`tools/ttl_receive.py` does the same in Python (`--port COM12`).

The instrument sends only to a port that the PC holds open (DTR set); both receivers set it.

## Notes on the hardware

The DSO-152 uses a CH32F103, a clone of the STM32F103. The USB serial of the Arduino STM32
core needs some corrections on it, all in `platformio.ini` and `src/UsbFix.cpp`:

- the CDC OUT endpoint single-buffered (`USBD_CDC_USE_SINGLE_BUFFER`);
- received packets taken in the interrupt into a 4 kB buffer, and the endpoint armed again
  there, instead of from the main loop, where the PC's retries occasionally halted the pipe;
- the transmit timeout raised from 3 ms to 500 ms;
- the port state read directly, without the 10 ms delay of `(bool)SerialUSB`;
- exports sent as single 63-byte packets, after a packet went missing in a long transfer.

The clock is taken from the 8 MHz crystal on the board (72 MHz system clock, 48 MHz for USB).
