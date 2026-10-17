/* Stand-in for the FNIRSI bootloader on a unit that no longer has one.
 * Sits at 0x08000000 and starts the image at 0x08003C00 the way the real
 * bootloader appears to: interrupts masked, stack and entry taken from the
 * image's own vector table. Only for testing dso152-bl builds over SWD. */
    .syntax unified
    .cpu cortex-m3
    .thumb
    .section .text
    .word 0x20005000
    .word start + 1
start:
    cpsid i
    ldr   r0, =0x08003C00
    ldr   r1, [r0]
    msr   msp, r1
    ldr   r0, [r0, #4]
    bx    r0
    .ltorg
