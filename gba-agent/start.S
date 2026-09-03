/*
 * PKHeX-GC GBA save-agent entry stub.
 *
 * A multiboot image is loaded at 0x02000000 and the BIOS enters it through
 * one of two fixed slots: 0x020000C0 for a Normal/Multiplay-mode transfer and
 * 0x020000E0 for a JOY-bus transfer.  PKHeX-GC boots over the JOY bus, but
 * both slots branch to the same start code so the image is also valid if it
 * is ever handed to a Normal-mode loader.
 */
.syntax unified
.cpu arm7tdmi
.arm

.section .text.entry,"ax",%progbits
.global _ram_entry
_ram_entry:                 /* 0x020000C0 - RAM entry point */
    b       _start
    .byte   0               /* 0x0C4 boot mode, written by the BIOS  */
    .byte   0               /* 0x0C5 slave ID, written by the BIOS   */
    .space  26              /* 0x0C6-0x0DF reserved                  */
.global _joy_entry
_joy_entry:                 /* 0x020000E0 - JOYBUS entry point */
    b       _start

.global _start
.type _start,%function
_start:
    ldr     sp, =0x03007F00
    /* The multiboot transfer carries .text/.data only, so clear .bss here. */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bl      agent_main
2:  b       2b
.size _start, .-_start
