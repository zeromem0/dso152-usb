#include <Arduino.h>
#include "Menu.h"
#include "Theme.h"
#include "Keys.h"
#include "Mascot.h"
#include "Version.h"

namespace Menu {

// The mascot was drawn on this colour and is edged with shades of it, so it
// always stands on a patch of it. In the default theme the whole screen is
// this colour and the patch does not show.
static const uint16_t MASCOT_BACK = 0x2104;

// There is room for four of these.
static const char* const ITEM[] = { "USB Serial", "USB Sender", "Memory", "Settings" };
static const int8_t NITEM = 4;
static int8_t g_sel = 0;

#define MASCOT_X   12
#define MASCOT_Y   50
#define ITEM_X     148
#define ITEM_Y     34
#define ITEM_W     160
#define ITEM_H     30
#define ITEM_STEP  38
#define FOOT_Y     196

// Each pixel of the picture is two by two on the screen. A row is sent as
// runs of one colour rather than pixel by pixel, which is a third as many
// rectangles.
static void drawMascot(DSO152TFT& tft) {
    if (TH.menuBg != MASCOT_BACK)
        tft.fillRect(MASCOT_X - 4, MASCOT_Y - 4, MASCOT_W * 2 + 8, MASCOT_H_PX * 2 + 8, MASCOT_BACK);

    for (uint16_t y = 0; y < MASCOT_H_PX; y++) {
        uint16_t x = 0;
        while (x < MASCOT_W) {
            uint32_t i  = (uint32_t)y * MASCOT_W + x;
            uint8_t  c  = (i & 1) ? (MASCOT_PIX[i >> 1] & 0x0F) : (MASCOT_PIX[i >> 1] >> 4);
            uint16_t x0 = x;
            for (x++; x < MASCOT_W; x++) {
                uint32_t j = (uint32_t)y * MASCOT_W + x;
                uint8_t  d = (j & 1) ? (MASCOT_PIX[j >> 1] & 0x0F) : (MASCOT_PIX[j >> 1] >> 4);
                if (d != c) break;
            }
            if (c)                          // entry 0 is the background, already there
                tft.fillRect(MASCOT_X + x0 * 2, MASCOT_Y + y * 2, (x - x0) * 2, 2, MASCOT_PAL[c]);
        }
    }
}

static void drawItem(DSO152TFT& tft, int8_t i) {
    int16_t  y   = ITEM_Y + i * ITEM_STEP;
    bool     on  = (i == g_sel);
    uint16_t bg  = on ? TH.sel : TH.menuBg;
    tft.fillRect(ITEM_X, y, ITEM_W, ITEM_H, bg);
    tft.drawRect(ITEM_X, y, ITEM_W, ITEM_H, on ? TH.selText : TH.line);
    tft.setTextSize(2);
    tft.setTextColor(on ? TH.selText : TH.muted, bg);
    tft.setCursor(ITEM_X + 6, y + 8);
    tft.print(ITEM[i]);
}

void draw(DSO152TFT& tft) {
    tft.fillScreen(TH.menuBg);

    drawMascot(tft);

    for (int8_t i = 0; i < NITEM; i++) drawItem(tft, i);

    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.menuBg);
    tft.setCursor(ITEM_X + 2, FOOT_Y);
    tft.print((char)0x1E); tft.print((char)0x1F);       // up and down arrows
    tft.print(" select   OK/RUN start");

    // which build this is, so that two of them are not mistaken for each other
    tft.setTextColor(TH.trace, TH.menuBg);
    tft.setCursor(ITEM_X + 2, FOOT_Y + 16);
    tft.print(FW_NAME " v" FW_VERSION);
    tft.setTextColor(TH.muted, TH.menuBg);
    tft.print("  " FW_DATE);
}

int8_t poll(DSO152TFT& tft) {
    int8_t was = g_sel;
    if (Keys::hit(Keys::UP)   && g_sel > 0)         g_sel--;
    if (Keys::hit(Keys::DOWN) && g_sel < NITEM - 1) g_sel++;
    if (g_sel != was) { drawItem(tft, was); drawItem(tft, g_sel); }

    if (Keys::hit(Keys::OK) || Keys::hit(Keys::RUN)) return g_sel;
    return -1;
}

}
