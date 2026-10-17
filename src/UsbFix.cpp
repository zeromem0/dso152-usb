// Corrections to the core's USB serial for the CH32F103 in the DSO-152.
//
// Receiving. The core keeps what the PC sends in a queue of a few packets,
// and when that is full it leaves the OUT endpoint answering NAK until the
// main loop reads and arms it again. On this chip that went wrong two ways:
//
//   The stock code, with the queue full, re-armed the endpoint with a buffer
//   of length zero (USBD_CDC_ClearBuffer). The next packet failed and
//   Windows halted the pipe for good: it stopped after 320 bytes every time.
//
//   With that taken out, the main loop arming the endpoint again while the
//   PC was retrying a NAKed packet, which it does back to back, still now and
//   then left the pipe halted: one export in three stopped after a kilobyte.
//
// So the packets are taken here, in the interrupt, into a buffer of our own,
// and the endpoint is armed again at once, in the same interrupt, right
// after the packet that came. Only when that buffer is full too is the
// endpoint left answering NAK, and armed again by usbRead() with the USB
// interrupt held off. The core's own queue stays empty and is not read:
// usbAvailable() and usbRead() take the place of SerialUSB.available() and
// read().
//
// The callback is reached through a table that the core keeps in RAM
// (USBD_CDC_fops), so it is replaced here rather than the library patched.

#include <Arduino.h>
#include "UsbFix.h"

#ifdef USBCON
extern "C" {
#include "usbd_cdc_if.h"
extern __IO bool dtrState;
extern USBD_HandleTypeDef hUSBD_Device_CDC;
}

#define RX_SIZE 4096                // a power of two
static uint8_t           g_rx[RX_SIZE];
static volatile uint16_t g_rxHead, g_rxTail;
static uint8_t*          g_block;   // where the packet memory is copied to
static volatile bool     g_held;    // the endpoint is left answering NAK

static uint16_t rxFree() { return (uint16_t)((g_rxTail - g_rxHead - 1) & (RX_SIZE - 1)); }

static void arm() {
    USBD_CDC_SetRxBuffer(&hUSBD_Device_CDC, g_block);
    USBD_CDC_ReceivePacket(&hUSBD_Device_CDC);
}

static int8_t receive(uint8_t* buf, uint32_t* len) {
    g_block = buf;
    for (uint32_t i = 0; i < *len; i++) {
        g_rx[g_rxHead] = buf[i];
        g_rxHead = (uint16_t)((g_rxHead + 1) & (RX_SIZE - 1));
    }
    if (rxFree() >= CDC_DATA_FS_MAX_PACKET_SIZE) arm();
    else                                         g_held = true;
    return (int8_t)USBD_OK;
}

// When the port is set up again (a new configuration from the PC), the core
// arms the endpoint itself; nothing is held any more.
static int8_t (*g_coreInit)(void);
static int8_t cdcInit(void) {
    g_held = false;
    return g_coreInit();
}

void usbFix() {
    g_coreInit = USBD_CDC_fops.Init;
    USBD_CDC_fops.Init    = cdcInit;
    USBD_CDC_fops.Receive = receive;
}

bool usbOpen() { return dtrState; }

int usbAvailable() { return (int)((g_rxHead - g_rxTail) & (RX_SIZE - 1)); }

int usbRead() {
    if (g_rxHead == g_rxTail) return -1;
    uint8_t b = g_rx[g_rxTail];
    g_rxTail = (uint16_t)((g_rxTail + 1) & (RX_SIZE - 1));
    if (g_held && rxFree() >= CDC_DATA_FS_MAX_PACKET_SIZE) {
        NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
        g_held = false;
        arm();
        NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
    }
    return b;
}
#else
void usbFix() {}
bool usbOpen() { return false; }
int  usbAvailable() { return 0; }
int  usbRead() { return -1; }
#endif
