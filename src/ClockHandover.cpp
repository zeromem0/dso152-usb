#include <Arduino.h>

// Replaces the core's weak SystemClock_Config. USB wants 48 MHz, and to an
// accuracy the internal oscillator does not have, so the clock comes from the
// crystal on the board (found working over SWD, 2026-10-16): 8 MHz x9 = 72
// MHz, and USB from that divided by 1.5. The first half is for the case where
// the FNIRSI bootloader starts this image.
//
// What the bootloader leaves behind is not known: it cannot be read out. If it
// hands over with the PLL already driving the system clock, HAL_RCC_OscConfig
// refuses to touch a PLL in use and the core stops in Error_Handler before
// setup() has latched the power on. So the clock is first put back to its
// reset state, and any interrupt the bootloader left enabled is silenced,
// since this image has no handler for it.
extern "C" void SystemClock_Config(void) {
    // Independent watchdog, about 4 s (LSI 40 kHz / 64, reload 2500), started
    // before anything that can stall and long before setup() latches the power
    // on through PC10. A hang then ends in a reset, PC10 floats and the unit
    // switches off. Without it a stalled image holds its own power and can
    // only be stopped by pulling the battery; that has happened once.
    // loop() reloads it.
    IWDG->KR  = 0x5555;
    IWDG->PR  = 4;
    IWDG->RLR = 2500;
    IWDG->KR  = 0xAAAA;
    IWDG->KR  = 0xCCCC;

    for (uint8_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    // The bootloader jumps with interrupts masked: the FNIRSI application has
    // exactly one CPSIE I, in main, and no CPSID anywhere. Left masked, SysTick
    // never counts and the first delay() never returns.
    __enable_irq();

    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    RCC->CFGR &= ~RCC_CFGR_SW;
    while (RCC->CFGR & RCC_CFGR_SWS) {}
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {}
    RCC->CIR = 0x009F0000u;
    SystemCoreClockUpdate();

    // The bootloader shows its drive over this same USB block. Whatever it
    // leaves set up there is put back to the reset state before the core
    // starts the port again.
    RCC->APB1RSTR |=  RCC_APB1RSTR_USBRST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USBRST;

    RCC_OscInitTypeDef osc = {};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    RCC_ClkInitTypeDef clk = {};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Error_Handler();

    RCC_PeriphCLKInitTypeDef per = {};
    per.PeriphClockSelection = RCC_PERIPHCLK_USB;
    per.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) Error_Handler();
}
