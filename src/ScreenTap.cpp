// Pictures of the screen without reading the screen. Everything that is
// drawn, text included, goes through DSO152TFT::fillRect, so in this build
// each rectangle is also put in a ring that a laptop empties over SWD
// (tools/screenshots.py) and paints into a picture of its own. What comes
// out is the screen pixel for pixel.
//
// When the ring is full the drawing waits for the laptop, which is why this
// is a build of its own (dso152-bl-shot) and not something left in.

#ifdef SCREEN_TAP

#include <Arduino.h>

// Two words a rectangle: x in 9 bits, y in 8, w in 9; then h in 8 and the
// colour in the upper half. The laptop reads tapHead, takes what lies between
// its own place and that, and writes tapTail.
#define TAP_N 192
volatile uint32_t tapHead = 0, tapTail = 0;
uint32_t tapBuf[2 * TAP_N];

void screenTap(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= 320 || y >= 240) return;
    if (x + w > 320) w = (int16_t)(320 - x);
    if (y + h > 240) h = (int16_t)(240 - y);

    uint32_t next = (tapHead + 1) % TAP_N;
    while (next == tapTail) { IWDG->KR = 0xAAAA; }      // the laptop is behind

    // w and h go in less one, so that 320 fits in nine bits and 240 in eight
    tapBuf[2 * tapHead]     = (uint32_t)x | ((uint32_t)y << 9) | ((uint32_t)(w - 1) << 17);
    tapBuf[2 * tapHead + 1] = (uint32_t)(h - 1) | ((uint32_t)color << 16);
    tapHead = next;
}

#endif
