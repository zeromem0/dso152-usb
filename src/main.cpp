// DSO-152 as a USB serial port: the instrument's own USB socket, the one the
// FNIRSI bootloader shows its drive on, made into a CDC virtual COM port by
// the core's USB device library (platformio.ini: USBCON, ENABLE_CDC).
//
// Three things to run from the menu: a terminal (what the PC sends is shown
// on the screen and sent back, and the buttons send lines to the PC); the USB
// Sender (TtlApp.cpp), which sends what the terminal received, or a stretch
// of flash, to the PC in a checked format; and Memory (MemApp.cpp), which
// shows the flash. Settings picks the theme, the export format and what the
// sender sends.
//
// USB wants 48 MHz from the crystal (ClockHandover.cpp). The board pulls D+
// up with a fixed resistor, so the core makes itself seen by holding D+ low
// for a moment at start.

#include <Arduino.h>
#include "dso152mod.h"
#include "Theme.h"
#include "Keys.h"
#include "Menu.h"
#include "SetApp.h"
#include "Config.h"
#include "Version.h"
#include "UsbFix.h"
#include "TtlApp.h"
#include "MemApp.h"

DSO152TFT tft;

#define TPRW          PC10

#define APP_MENU    0
#define APP_SERIAL  1
#define APP_SET     2
#define APP_SENDER  3
#define APP_MEM     4

// What the terminal received, as it came, for the sender. When it is full the
// older half goes.
#define BUFFER_SIZE 2048
static char dataBuffer[BUFFER_SIZE];
static int  bufferIndex = 0;

static void keep(const uint8_t* p, int n) {
    for (int i = 0; i < n; i++) {
        if (bufferIndex >= BUFFER_SIZE) {
            memmove(dataBuffer, dataBuffer + BUFFER_SIZE / 2, BUFFER_SIZE / 2);
            bufferIndex = BUFFER_SIZE / 2;
        }
        dataBuffer[bufferIndex++] = (char)p[i];
    }
}
static uint8_t app = APP_MENU;

// --- the terminal ---------------------------------------------------------------
#define T_COLS   52
#define T_ROWS   17
#define T_Y      24
#define T_LINE_H 11

static char     g_text[T_ROWS][T_COLS + 1];
static uint8_t  g_row, g_col;
static uint32_t g_in, g_out;
static bool     g_open;             // the PC has the port open (DTR)

// The screen is behind the data on purpose: what comes in is read and sent
// back at once, and painted at most ten times a second. Painted as it came,
// 64 bytes at a time, the line would wait for the screen.
static bool     g_all;              // everything moved up: every row to repaint
static uint8_t  g_from = 0xFF;      // else the first row changed since the last paint
static uint32_t g_paintMs;
volatile bool   g_echo = true;      // for tests over SWD: the two directions apart
volatile uint16_t g_burst;          // for tests: this many bytes to send, then 0

static void usbOut(const char* s) {
#ifdef USBCON
    uint32_t n = strlen(s);
    if (usbOpen()) { SerialUSB.write((const uint8_t*)s, n); g_out += n; }
#else
    (void)s;
#endif
}

static void drawRow(uint8_t r) {
    int16_t y = (int16_t)(T_Y + r * T_LINE_H);
    tft.fillRect(0, y, 320, T_LINE_H, TH.bg);
    tft.setTextSize(1);
    tft.setTextColor(TH.text, TH.bg);
    tft.setCursor(4, y + 1);
    tft.print(g_text[r]);
}

static void drawBar() {
    tft.fillRect(0, 0, 320, 20, TH.bar);
    tft.setTextSize(1);
    tft.setTextColor(TH.barText, TH.bar);
    tft.setCursor(2, 6);
    tft.print("USB SERIAL");
    tft.setCursor(80, 6);
#ifdef USBCON
    tft.print(g_open ? "port open" : "port closed");
#else
    tft.print("no USB in this build");
#endif
    tft.setCursor(170, 6);
    tft.print("in "); tft.print(g_in);
    tft.setCursor(240, 6);
    tft.print("out "); tft.print(g_out);
}

static void drawHint() {
    tft.fillRect(0, 214, 320, 26, TH.bg);
    tft.drawFastHLine(0, 212, 320, TH.line);
    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(4, 218);
    tft.print("buttons are sent to the PC   RUN: a test line");
    tft.setCursor(4, 230);
    tft.print("AUTO held: clear the screen   OK held: menu");
}

static void clearText() {
    memset(g_text, 0, sizeof(g_text));
    g_row = 0; g_col = 0;
}

// One character on the screen: CR goes to the start of the line, LF to the
// next one, and what does not print shows as a dot. With the bottom line
// full, everything moves up by one.
static void touched(uint8_t r) { if (r < g_from) g_from = r; }

static void newLine() {
    g_col = 0;
    if (g_row + 1 < T_ROWS) { g_row++; touched(g_row); return; }
    memmove(g_text[0], g_text[1], sizeof(g_text) - sizeof(g_text[0]));
    memset(g_text[T_ROWS - 1], 0, sizeof(g_text[0]));
    g_all = true;
}

static void put(uint8_t c) {
    if (c == '\r') { g_col = 0; return; }
    if (c == '\n') { newLine(); return; }
    if (g_col >= T_COLS) newLine();
    touched(g_row);
    g_text[g_row][g_col++] = (char)((c >= 32 && c < 127) ? c : '.');
    g_text[g_row][g_col] = 0;
}

static void paint(bool now) {
    if (!g_all && g_from == 0xFF) return;
    if (!now && millis() - g_paintMs < 100) return;
    g_paintMs = millis();
    for (uint8_t r = g_all ? 0 : g_from; r < T_ROWS; r++) drawRow(r);
    g_all = false;
    g_from = 0xFF;
    drawBar();
}

static void serialEnter() {
    Keys::sync();
    tft.fillScreen(TH.bg);
    drawBar();
    for (uint8_t r = 0; r < T_ROWS; r++) drawRow(r);
    drawHint();
}

// One turn of the terminal. False: OK held, back to the menu.
static bool serialLoop() {

#ifdef USBCON
    // The greeting waits a tenth of a second after the port is opened: a PC
    // program empties what it has received while opening, and one sent at
    // once would be thrown away with it.
    static uint32_t openMs;
    static bool     greeted;
    bool open = usbOpen();
    if (open != g_open) {
        g_open = open;
        openMs = millis();
        greeted = false;
        drawBar();
    }
    if (open && !greeted && millis() - openMs >= 100) {
        greeted = true;
        usbOut("DSO-152 " FW_NAME " v" FW_VERSION ": connected\r\n");
        drawBar();
    }
    // all there is, up to two kilobytes a turn, a packet at a time
    uint8_t buf[64];
    for (uint8_t k = 0; k < 32; k++) {
        int n = 0;
        while (n < (int)sizeof(buf) && usbAvailable()) buf[n++] = (uint8_t)usbRead();
        if (!n) break;
        g_in += (uint32_t)n;
        if (g_echo) {
            SerialUSB.write(buf, (size_t)n);    // echoed, so that a terminal shows what is typed
            g_out += (uint32_t)n;
        }
        for (int i = 0; i < n; i++) put(buf[i]);
        keep(buf, n);
    }
    if (g_burst) {
        static uint8_t pat[64];
        for (uint8_t i = 0; i < 64; i++) pat[i] = (uint8_t)('A' + (i % 26));
        while (g_burst) {
            uint16_t n = g_burst < 64 ? g_burst : 64;
            SerialUSB.write(pat, n);
            g_out += n;
            g_burst = (uint16_t)(g_burst - n);
            IWDG->KR = 0xAAAA;
        }
        drawBar();
    }
#endif

    static const char* const NAME[Keys::COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "OK", "MODE", "AUTO", "RUN", "RELEARN" };
    for (uint8_t k = 0; k < Keys::COUNT; k++) {
        if (k == Keys::OK || k == Keys::AUTO) continue;     // read below, short and held
        if (Keys::hit(k)) {
            char line[40];
            if (k == Keys::RUN) snprintf(line, sizeof(line), "test line, %lu s since start\r\n", (unsigned long)(millis() / 1000));
            else                snprintf(line, sizeof(line), "key %s\r\n", NAME[k]);
            usbOut(line);
            drawBar();
        }
    }
    uint8_t ok = Keys::press(Keys::OK);
    if (ok == Keys::LONG) return false;
    if (ok == Keys::SHORT) { usbOut("key OK\r\n"); drawBar(); }
    uint8_t au = Keys::press(Keys::AUTO);
    if (au == Keys::SHORT) { usbOut("key AUTO\r\n"); drawBar(); }
    if (au == Keys::LONG)  { clearText(); g_all = true; paint(true); }

    paint(false);
    return true;
}

static void showMenu() {
    app = APP_MENU;
    Keys::sync();
    Menu::draw(tft);
}

void setup() {
    Keys::begin();

    pinMode(TPRW, OUTPUT);
    digitalWrite(TPRW, HIGH);
    delay(300);

    Config::load();                     // the theme, before anything is drawn
    tft.begin();
    clearText();
#ifdef USBCON
    usbFix();                           // the receive path, made to work on this chip
    SerialUSB.begin();
#endif
    showMenu();
}

void loop() {
    IWDG->KR = 0xAAAA;                  // started in ClockHandover.cpp

    if (digitalRead(POWER_BUTTON) == LOW) {
        digitalWrite(TPRW, LOW);
        delay(200);
    }

    switch (app) {
        case APP_MENU: {
            int8_t pick = Menu::poll(tft);
            if (pick == Menu::ITEM_SERIAL)      { app = APP_SERIAL; serialEnter(); }
            else if (pick == Menu::ITEM_SENDER) { app = APP_SENDER; Ttl::enter(tft, dataBuffer, bufferIndex); }
            else if (pick == Menu::ITEM_MEM)    { app = APP_MEM;    Memory::enter(tft); }
            else if (pick == Menu::ITEM_SET)    { app = APP_SET;    Settings::enter(tft, bufferIndex); }
            break;
        }
        case APP_SET: {
            uint8_t r = Settings::poll(tft);
            if (r == Settings::ERASE)      { bufferIndex = 0; Settings::erased(tft); }
            else if (r == Settings::LEAVE) showMenu();
            break;
        }
        case APP_SENDER:
            if (!Ttl::poll(tft, dataBuffer, bufferIndex)) { Ttl::leave(); showMenu(); }
            break;
        case APP_MEM:
            if (!Memory::poll(tft)) showMenu();
            break;
        case APP_SERIAL:
            if (!serialLoop()) showMenu();
            break;
    }
}
