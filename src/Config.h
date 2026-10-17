// The settings that are kept over a power cycle: the theme, and how the
// sender exports.
//
// They live in the last page of the application's part of flash, 0x0800F800
// to 0x0800FBFF, directly under the stock firmware's calibration page at
// 0x0800FC00, which is never touched. platformio.ini ends the program at
// 0x0800F800 so that the two cannot meet. This page is the only thing in
// flash this firmware ever erases or writes.

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

namespace Config {

extern uint8_t format;          // Export::RAW, DUMP or IHEX
extern uint8_t block;           // which of the block sizes

// What the sender sends: 0 what the terminal received, k the flash area
// Memory::AREA[k - 1]. Not kept: every start is with the buffer.
extern uint8_t source;

const uint8_t NBLOCK = 4;
uint16_t blockSize();           // 0: everything in one
uint16_t blockSizeAt(uint8_t i);

void load();                    // at start; defaults if nothing valid is there
bool save();                    // only writes if something differs; false: it did not take

}
#endif
