#include <Arduino.h>
#include "MemApp.h"
#include "Theme.h"
#include "Keys.h"
#include "Config.h"
#include "DisplayChars.h"

namespace Memory {

// The stretches there are to look at, in address order. The FNIRSI bootloader
// comes first (read protection keeps the debugger out of it, not the code that
// runs from flash); the program is linked to end where the settings page
// begins (platformio.ini), and the stock calibration follows.
const Area AREA[] = {
    { "Bootloader",  0x08000000u, 0x08003C00u - 0x08000000u },
    { "Application", 0x08003C00u, 0x0800F800u - 0x08003C00u },
    { "Settings",    0x0800F800u, 1024 },
    { "Calibration", 0x0800FC00u, 1024 },
};
const uint8_t NAREA = sizeof(AREA) / sizeof(AREA[0]);

static uint8_t g_area = 1;          // start on the application
static bool    g_hex  = true;
static int32_t g_first = 0;         // the first line on the screen

// A hex dump in the usual shape: the full address, then 16 bytes in two
// groups of 8. The text of the bytes is left to the character view.
#define DUMP_PER_LINE  16
#define DUMP_LINES     17           // lines of 12 from y = 20; the hint goes below
#define DUMP_LINE_H    12
#define DUMP_HEX_X     57           // after 8 address digits and a space
#define DUMP_PITCH     16           // two hex digits and 4 pixels
#define DUMP_GROUP     4            // more after the eighth byte

static void printHex(DSO152TFT& tft, uint32_t v, uint8_t digits) {
    while (digits--) tft.print((char)"0123456789ABCDEF"[(v >> (digits * 4)) & 0xF]);
}

static void drawDump(DSO152TFT& tft, uint32_t base, uint32_t size, int32_t firstLine) {
    const uint8_t* p = (const uint8_t*)base;
    tft.setTextSize(1);
    for (int i = 0; i < DUMP_LINES; i++) {
        int16_t  y   = (int16_t)(20 + i * DUMP_LINE_H);
        uint32_t off = (uint32_t)(firstLine + i) * DUMP_PER_LINE;
        tft.fillRect(0, y, 320, DUMP_LINE_H, TH.bg);
        if (off >= size) continue;

        tft.setTextColor(TH.muted, TH.bg);
        tft.setCursor(3, y);
        printHex(tft, base + off, 8);

        tft.setTextColor(TH.text2, TH.bg);
        for (uint8_t k = 0; k < DUMP_PER_LINE && off + k < size; k++) {
            tft.setCursor((int16_t)(DUMP_HEX_X + k * DUMP_PITCH + (k >= 8 ? DUMP_GROUP : 0)), y);
            printHex(tft, p[off + k], 2);
        }
    }
}

static int32_t perLine() { return g_hex ? DUMP_PER_LINE : CHARS_PER_LINE; }
static int32_t visible() { return g_hex ? DUMP_LINES : CHARS_VIS_LINES; }
static int32_t lines()   { return (int32_t)((AREA[g_area].size + perLine() - 1) / perLine()); }

static void clamp() {
    int32_t last = lines() - visible();
    if (g_first > last) g_first = last;
    if (g_first < 0)    g_first = 0;
}

static void draw(DSO152TFT& tft) {
    const Area& a = AREA[g_area];

    tft.fillRect(0, 0, 320, 20, TH.bar);
    tft.setTextSize(1);
    tft.setTextColor(TH.barText, TH.bar);
    tft.setCursor(2, 6);
    tft.print(a.name);
    tft.setCursor(80, 6);
    printHex(tft, a.base, 8); tft.print('-'); printHex(tft, a.base + a.size - 1, 8);
    tft.setCursor(200, 6);
    tft.print(g_hex ? "hex" : "chars");
    // where on the stretch the screen is, as the offset of its first byte
    tft.setCursor(248, 6);
    tft.print('+');
    printHex(tft, (uint32_t)g_first * (uint32_t)perLine(), 4);

    if (g_hex) {
        drawDump(tft, a.base, a.size, g_first);
        // the character view reaches lower than the dump: clear what it left
        tft.fillRect(0, 20 + DUMP_LINES * DUMP_LINE_H, 320, 229 - (20 + DUMP_LINES * DUMP_LINE_H), TH.bg);
    } else {
        tft.fillRect(0, 20, 320, 210, TH.bg);
        displayChars(tft, (const char*)a.base, (int)a.size, (int)g_first);
    }

    tft.fillRect(0, 229, 320, 11, TH.bg);
    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(4, 231);
    tft.print((char)0x1E); tft.print((char)0x1F);       // up and down arrows
    tft.print(" scroll ");
    tft.print((char)0x11); tft.print((char)0x10);       // left and right arrows
    tft.print(" 1kB  AUTO area  OK hex/chr  MODE menu");
}

void enter(DSO152TFT& tft) {
    Keys::sync();
    tft.fillScreen(TH.bg);
    clamp();
    draw(tft);
}

bool poll(DSO152TFT& tft) {
    int32_t was = g_first;
    uint8_t area = g_area;
    bool    hex = g_hex;

    if (Keys::hit(Keys::DOWN))  g_first += visible() / 2;
    if (Keys::hit(Keys::UP))    g_first -= visible() / 2;
    if (Keys::hit(Keys::RIGHT)) g_first += 1024 / perLine();
    if (Keys::hit(Keys::LEFT))  g_first -= 1024 / perLine();
    if (Keys::hit(Keys::AUTO))  { g_area = (uint8_t)((g_area + 1) % NAREA); g_first = 0; }

    uint8_t ok = Keys::press(Keys::OK);
    if (ok == Keys::LONG) return false;
    if (ok == Keys::SHORT) {
        // keep the same bytes at the top when the line length changes
        uint32_t at = (uint32_t)g_first * (uint32_t)perLine();
        g_hex = !g_hex;
        g_first = (int32_t)(at / (uint32_t)perLine());
    }
    if (Keys::hit(Keys::MODE)) return false;

    clamp();
    if (g_first != was || g_area != area || g_hex != hex) draw(tft);
    return true;
}

}
