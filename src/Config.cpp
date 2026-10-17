#include <Arduino.h>
#include "Config.h"
#include "Theme.h"
#include "Export.h"

namespace Config {

static const uint32_t PAGE  = 0x0800F800u;      // see Config.h
static const uint16_t MAGIC = 0xD152;
// The layout of the record: 1 is that of the TTL and Tester232 firmwares,
// whose format and block are the same as here, so their record is read too;
// 3 is the Modbus tester's, which is not, and is left alone.
static const uint8_t  LAYOUT = 1;

static const uint16_t BLOCK[NBLOCK] = { 0, 64, 256, 1024 };

uint8_t format = Export::DUMP;
uint8_t block  = 0;
uint8_t source = 0;

uint16_t blockSizeAt(uint8_t i) { return i < NBLOCK ? BLOCK[i] : 0; }
uint16_t blockSize() { return blockSizeAt(block); }

// Four half words: the mark, layout and theme, format and block, and a check
// over the three before it.
static void pack(uint16_t w[4]) {
    w[0] = MAGIC;
    w[1] = (uint16_t)(LAYOUT | (Theme::idx << 8));
    w[2] = (uint16_t)(format | (block << 8));
    w[3] = (uint16_t)((w[0] + w[1] + w[2]) ^ 0xA55Au);
}

static inline uint16_t stored(uint8_t i) { return *(const volatile uint16_t*)(PAGE + 2u * i); }

void load() {
    uint16_t w[4];
    for (uint8_t i = 0; i < 4; i++) w[i] = stored(i);
    if (w[0] != MAGIC || (w[1] & 0xFF) != LAYOUT) return;
    if (w[3] != (uint16_t)((w[0] + w[1] + w[2]) ^ 0xA55Au)) return;

    uint8_t t = (uint8_t)(w[1] >> 8), f = (uint8_t)(w[2] & 0xFF), b = (uint8_t)(w[2] >> 8);
    if (t < Theme::count())  Theme::idx = t;
    if (f < Export::NFMT)    format = f;
    if (b < NBLOCK)          block = b;
}

// Waits for the flash controller, for no longer than an erase can take.
static bool settle() {
    uint32_t t0 = millis();
    while (FLASH->SR & FLASH_SR_BSY) {
        if ((millis() - t0) > 200) return false;
    }
    bool ok = !(FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR));
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
    return ok;
}

bool save() {
    uint16_t w[4];
    pack(w);
    bool same = true;
    for (uint8_t i = 0; i < 4; i++) if (stored(i) != w[i]) same = false;
    if (same) return true;

    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
    bool ok = settle();

    if (ok) {
        FLASH->CR |= FLASH_CR_PER;
        FLASH->AR  = PAGE;
        FLASH->CR |= FLASH_CR_STRT;
        ok = settle();
        FLASH->CR &= ~FLASH_CR_PER;
    }
    for (uint8_t i = 0; ok && i < 4; i++) {
        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint16_t*)(PAGE + 2u * i) = w[i];
        ok = settle();
        FLASH->CR &= ~FLASH_CR_PG;
    }
    FLASH->CR |= FLASH_CR_LOCK;

    for (uint8_t i = 0; ok && i < 4; i++) if (stored(i) != w[i]) ok = false;
    return ok;
}

}
