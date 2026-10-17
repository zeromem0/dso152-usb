// A correction to the core's USB serial for the CH32F103 in the DSO-152.
//
// When a packet from the PC fills the core's receive queue, its receive
// callback re-arms the OUT endpoint with a buffer of length zero
// (USBD_CDC_ClearBuffer). On a genuine STM32 that may pass; on this chip the
// next 64-byte packet fails, Windows halts the pipe for good, and nothing more
// reaches the instrument: it stopped after 320 bytes, five packets, every time.
//
// The callback is reached through a table that the core keeps in RAM
// (USBD_CDC_fops), so it is replaced here rather than the library patched.
// The replacement leaves the endpoint as the hardware leaves it after a
// packet, answering NAK, so the PC simply tries again, until reading makes
// room and CDC_resume_receive() arms it once more.

#ifdef USBCON
#include <Arduino.h>
extern "C" {
#include "usbd_cdc_if.h"
extern __IO bool receivePended;
extern __IO bool dtrState;
bool CDC_resume_receive(void);
}
#include "UsbFix.h"

static int8_t receive(uint8_t* buf, uint32_t* len) {
    (void)buf;
    CDC_ReceiveQueue_CommitBlock(&ReceiveQueue, (uint16_t)(*len));
    receivePended = false;
    (void)CDC_resume_receive();         // false: the queue is full; read() resumes it
    return (int8_t)USBD_OK;
}

void usbFix() { USBD_CDC_fops.Receive = receive; }

bool usbOpen() { return dtrState; }
#else
bool usbOpen() { return false; }
#endif
