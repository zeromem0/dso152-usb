#include "Keys.h"

namespace Keys {

static const uint32_t PIN[COUNT] = {
    UP_BUTTON, DOWN_BUTTON, LEFT_BUTTON, RIGHT_BUTTON,
    OK_BUTTON, MODE_BUTTON, AUTO_BUTTON, RUN_BUTTON, RELEARN_BUTTON
};

static const uint32_t SETTLE_MS = 60;
static const uint32_t LONG_MS   = 700;

static bool     g_last[COUNT];
static uint32_t g_upMs[COUNT];      // when the key was last seen down

// for press(): a key is not armed while it is still down from the screen before
static bool     g_armed[COUNT], g_held[COUNT], g_fired[COUNT];
static uint32_t g_since[COUNT];

void begin() {
    for (uint8_t k = 0; k < COUNT; k++) pinMode(PIN[k], INPUT_PULLUP);
    pinMode(POWER_BUTTON, INPUT_PULLUP);
    sync();
}

bool down(uint8_t k) { return k < COUNT && digitalRead(PIN[k]) == LOW; }

void sync() {
    for (uint8_t k = 0; k < COUNT; k++) {
        g_last[k]  = down(k);
        g_upMs[k]  = millis() - (g_last[k] ? 0 : SETTLE_MS);
        g_armed[k] = !g_last[k];
        g_held[k]  = false;
        g_fired[k] = false;
    }
}

bool hit(uint8_t k) {
    if (k >= COUNT) return false;
    bool d = down(k);
    uint32_t now = millis();
    bool h = d && !g_last[k] && (now - g_upMs[k]) >= SETTLE_MS;
    if (d) g_upMs[k] = now;
    g_last[k] = d;
    return h;
}

uint8_t press(uint8_t k) {
    if (k >= COUNT) return 0;
    bool d = down(k);
    uint32_t now = millis();

    if (!g_armed[k]) {
        if (!d) g_armed[k] = true;
        return 0;
    }
    if (d) {
        if (!g_held[k]) { g_held[k] = true; g_since[k] = now; g_fired[k] = false; }
        else if (!g_fired[k] && (now - g_since[k]) >= LONG_MS) { g_fired[k] = true; return LONG; }
        return 0;
    }
    if (g_held[k]) {
        uint32_t held = now - g_since[k];
        g_held[k] = false;
        if (!g_fired[k] && held >= 30) return SHORT;
    }
    return 0;
}

}
