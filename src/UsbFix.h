// See UsbFix.cpp: the core's USB serial receive path, made to work on the
// CH32F103. Call before SerialUSB.begin().

#ifndef USBFIX_H
#define USBFIX_H

void usbFix();

// Whether the PC has the port open (DTR). The core's `(bool)SerialUSB` says the
// same after a delay(10) of its own, which a check in every turn or before
// every packet cannot afford: it held the sender to 6 kB/s.
bool usbOpen();

#endif
