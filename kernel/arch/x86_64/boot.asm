;==============================================================================
; ARES OS - Kernel Entry Point (64-bit)
;==============================================================================
; This is the first code executed when the bootloader transfers control
; to the kernel. It runs in 64-bit long mode.
;
; Responsibilities:
;   1. Set up the kernel stack
;   2. Clear the BSS section
;   3. Set up initial kernel environment
;   4. Call kernel_main()
;
; Build: x86_64-elf-as -o boot.o boot.asm
;==============================================================================

[bits 64]
[global _start]
[extern kernel_main]
[extern console_init]
[extern console_puts]
[extern _kernel_bss_start]
[extern _kernel_bss_end]

; Kernel stack size: 16KB
KERNEL_STACK_SIZE equ 0x4000

;==============================================================================
; Entry Point
;==============================================================================
section .text
_start:
    ; Jump over the kernel signature
    jmp .entry_start
    ; Kernel signature for bootloader verification (8 bytes)
    db "ARESKERN"
.entry_start:
    ; Clear base pointer (mark end of call chain for stack trace)
    xor rbp, rbp

    ; Set up kernel stack (grows downward from KERNEL_STACK_ADDR)
    lea rsp, [kernel_stack_top]
    mov rbp, rsp

    ; Clear BSS section
    mov rdi, _kernel_bss_start
    mov rcx, _kernel_bss_end
    sub rcx, rdi                ; Size of BSS
    xor al, al
    cld
    rep stosb

    ; Save the boot drive number (passed from bootloader in rbx)
    ; (Not used for now, but preserved for future use)

    ; Print kernel banner
    mov rdi, boot_banner
    call console_puts

    ; Call kernel main
    call kernel_main

    ; Kernel_main should never return; if it does, halt
    cli
.halt_loop:
    hlt
    jmp .halt_loop

;==============================================================================
; Data Section
;==============================================================================
section .data
boot_banner:
    db "ARES OS Kernel Entry", 13, 10
    db "====================", 13, 10
    db "Version: 0.1.0", 13, 10
    db "Arch: x86_64", 13, 10
    db "Mode: Long Mode (64-bit)", 13, 10, 13, 10, 0

;==============================================================================
; BSS Section
;==============================================================================
section .bss
    ; Kernel stack space
    align 16
kernel_stack_bottom:
    resb KERNEL_STACK_SIZE
kernel_stack_top:
