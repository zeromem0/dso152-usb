// Memory: the flash looked at as it stands, a stretch at a time.
//
//   AUTO              which stretch: the application, the page the settings
//                     are kept in, the stock calibration, the bootloader
//   up, down          half a screen back or on
//   left, right       a kilobyte back or on
//   OK                hex (address and 16 bytes a line) or characters
//   MODE, or OK held  back to the menu
//
// Only read: nothing here writes anything.

#ifndef MEMAPP_H
#define MEMAPP_H

#include "dso152mod.h"

namespace Memory {

// The stretches of flash there are, in address order; the USB Sender can
// send any of them instead of the buffer (Settings, Source).
struct Area { const char* name; uint32_t base; uint32_t size; };
extern const Area    AREA[];
extern const uint8_t NAREA;

void enter(DSO152TFT& tft);
bool poll(DSO152TFT& tft);      // false: leave

}
#endif
