// The start screen: the mascot, what there is to run, and which build this is.
// Up and down move the choice, OK or RUN starts it.

#ifndef MENU_H
#define MENU_H

#include "dso152mod.h"
#include <stdint.h>

namespace Menu {

const int8_t ITEM_SERIAL = 0;
const int8_t ITEM_SENDER = 1;
const int8_t ITEM_MEM    = 2;
const int8_t ITEM_SET    = 3;

void   draw(DSO152TFT& tft);
int8_t poll(DSO152TFT& tft);    // the item started, or -1

}
#endif
