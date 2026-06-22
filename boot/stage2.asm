;==============================================================================
; ARES OS - Stage 2 Bootloader
;==============================================================================
; Stage 2 is loaded by Stage 1 (MBR) at 0x7E00.
;
; Responsibilities:
;   1. Enable A20 gate for memory above 1MB
;   2. Load kernel binary from disk to physical 0x100000
;   3. Verify kernel integrity via magic signature
;   4. Set up Global Descriptor Table (GDT) for long mode
;   5. Build 4-level page tables (PML4, PDPT, PD with 2MB pages)
;   6. Enable PAE + Long Mode + Paging
;   7. Far jump into 64-bit long mode
;   8. Jump to kernel entry point at 0x100000
;
; Memory Layout:
;   0x7C00 - 0x7DFF : Stage 1 (MBR)
;   0x7E00 - 0xFC00 : Stage 2 (this code)
;   0x1000 - 0x3FFF : Page tables (PML4, PDPT, PD)
;   0x100000        : Kernel image
;
; Signature at word 510: 0xA55A (for Stage 1 verification)
;
; Build: nasm -f bin -o stage2.bin stage2.asm
;==============================================================================

[org 0x7E00]
[bits 16]

;==============================================================================
; ENTRY POINT
;==============================================================================
entry:
    ; Save boot drive number (from BIOS in DL)
    mov [boot_drive], dl

    ; Set up real-mode segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFB00              ; Stack near top of stage 2 region

    ; Print boot banner
    mov si, msg_banner
    call print_string_16

;==============================================================================
; STEP 1: ENABLE A20 GATE FIRST
;==============================================================================
; Must enable A20 before accessing memory above 1MB (kernel load at 0x100000).
; INT 13h uses the CPU to copy data, so A20 affects disk reads to high memory.

    mov si, msg_a20
    call print_string_16

    ; Method 1: BIOS INT 15h AX=2401
    mov ax, 0x2401
    int 0x15
    call check_a20
    jnz .a20_ok

    ; Method 2: Keyboard controller
    call a20_keyboard
    call check_a20
    jnz .a20_ok

    ; Method 3: Fast A20 via port 0x92
    call a20_fast
    call check_a20
    jnz .a20_ok

    ; All methods failed
    mov si, msg_a20_fail
    call print_string_16
    cli
    hlt

.a20_ok:
    mov si, msg_a20_ok
    call print_string_16

;==============================================================================
; STEP 2: LOAD KERNEL FROM DISK
;==============================================================================
; Kernel is a raw binary at LBA sectors 65+.
; Load to buffer at 0x10000 (later copied to 0x100000 in long mode).
;
; DAP addressing: physical = segment * 16 + offset
;   0xF000 * 16 + 0x1000 = 0xF0000 + 0x1000 = 0x100000

    mov si, msg_loading_kernel
    call print_string_16

    ; INT 13h AH=42h supports at most ~127 sectors per call.
    ; Kernel is ~137 sectors, so load in two chunks:
    ;   DAP1: 127 sectors to buffer (0x8200)
    ;   DAP2: remaining 10 sectors to buffer + 127*512
    mov si, dap_kernel
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .load_failed

    mov si, dap_kernel2
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .load_failed

    ; Verify kernel magic signature (at offset 2, after the jmp instruction)
    ; Check at buffer address 0x8200 (load buffer, above stage2)
    ; Kernel will be copied to 0x100000 after entering long mode
    ; Kernel binary layout: jmp (2B) + "ARESKERN" (8B)
    mov si, 0x8202
    cmp dword [si], 0x53455241  ; "ARES" in LE
    jne .bad_kernel

    mov si, 0x8206
    cmp dword [si], 0x4E52454B  ; "KERN" in LE
    jne .bad_kernel

    mov si, msg_kernel_ok
    call print_string_16

    ; Debug: Write 'R' to serial port COM1 (verify serial access in 16-bit mode)
    mov dx, 0x3F8
    mov al, 'R'
    out dx, al
    mov al, 0x0D
    out dx, al
    mov al, 0x0A
    out dx, al

    jmp .load_done

.load_failed:
    mov si, msg_disk_err
    call print_string_16
    mov si, msg_press_key
    call print_string_16
    xor ax, ax
    int 0x16                    ; Wait for keypress
    mov ah, 0x00
    mov dl, [boot_drive]
    int 0x13                    ; Reset disk system
    jmp entry                   ; Retry from start

.bad_kernel:
    mov si, msg_bad_kernel
    call print_string_16
    cli
    hlt

.load_done:

;==============================================================================
; STEP 3: LOAD GLOBAL DESCRIPTOR TABLE
;==============================================================================
    mov si, msg_gdt
    call print_string_16
    lgdt [gdtr]

;==============================================================================
; STEP 4: SET UP PAGE TABLES FOR LONG MODE
;==============================================================================
; Long mode requires 4-level paging even with 2MB pages.
;
; Page tables at fixed physical addresses:
;   PML4  at 0x1000  - Page Map Level 4
;   PDPT  at 0x2000  - Page Directory Pointer Table
;   PD    at 0x3000  - Page Directory (2MB page entries)
;
; Identity-map the first 16MB using 8x 2MB pages.
; This covers: kernel at 1MB, page tables, VGA, BIOS areas.

    mov si, msg_paging
    call print_string_16

    ; Clear 12KB page table region (0x1000 - 0x3FFF)
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x3000
    cld
    rep stosb

    ; PML4[0] -> PDPT at 0x2000 (Present | R/W)
    mov edi, 0x1000
    mov eax, 0x2000
    or eax, 0x03
    mov [edi], eax

    ; PDPT[0] -> PD at 0x3000 (Present | R/W)
    mov edi, 0x2000
    mov eax, 0x3000
    or eax, 0x03
    mov [edi], eax

    ; PD[0..7] -> 8x 2MB pages covering 0x00000000 - 0x01000000 (16MB)
    ; Each entry: Present | R/W | PS (2MB page)
    mov edi, 0x3000
    mov eax, 0x000083            ; Present | R/W | PS=1 (2MB page)
    mov ecx, 8

.pd_loop:
    mov [edi], eax
    add eax, 0x200000            ; Next 2MB physical block
    add edi, 8
    loop .pd_loop

    ; Load PML4 base address into CR3
    mov eax, 0x1000
    mov cr3, eax

;==============================================================================
; STEP 5: ENABLE LONG MODE AND ENTER 64-BIT
;==============================================================================
    ; 5a: Enable PAE (Physical Address Extension) in CR4
    mov eax, cr4
    or eax, (1 << 5)            ; CR4.PAE = 1
    or eax, (1 << 7)            ; CR4.PGE = 1 (Global Pages)
    mov cr4, eax

    ; 5b: Set LME (Long Mode Enable) and NXE in EFER MSR
    mov ecx, 0xC0000080         ; EFER MSR address
    rdmsr
    or eax, (1 << 8)            ; EFER.LME = 1
    or eax, (1 << 11)           ; EFER.NX = 1 (Execute Disable)
    wrmsr

    ; 5c: Enable Protected Mode only (no paging yet).
    ; After PE=1, CS still has real-mode attributes (limit=64K, 16-bit).
    ; Need a far jump to reload CS with a proper protected mode descriptor.
    mov eax, cr0
    or eax, (1 << 0)            ; CR0.PE = 1 (Protected Mode)
    mov cr0, eax

    ; Far jump to 32-bit protected mode to reload CS with selector 0x18.
    ; CPU is now in 16-bit protected mode (but CS limit still 64K from real mode).
    ; Manually encode far jump with 0x66 prefix for 32-bit offset.
    db 0x66                     ; operand size override (16-bit -> 32-bit in 16-bit CS)
    db 0xEA                     ; far jump opcode
    dd prot_mode32              ; 32-bit target offset
    dw 0x18                     ; 32-bit code segment selector

;==============================================================================
; 32-BIT PROTECTED MODE ENTRY
;==============================================================================
[bits 32]
prot_mode32:
    ; Set up segment registers with 32-bit data segment
    mov ax, 0x20                ; 32-bit data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up temporary stack
    mov esp, 0xFB00
    mov ebp, esp

    ; 5d: Enable paging (Protected Mode already active from step 5c)
    mov eax, cr0
    or eax, (1 << 31)           ; CR0.PG = 1 (Paging)
    or eax, (1 << 16)           ; CR0.WP = 1 (Write Protect)
    mov cr0, eax

    ; 5e: Far jump to 64-bit long mode.
    ; CPU is now in 32-bit compatibility mode with PE+PG active.
    ; Far jump with 64-bit code segment transitions to long mode.
    ; In [bits 32], NASM encodes jmp with 32-bit offset by default.
    jmp 0x08:long_mode_entry

;==============================================================================
; 16-BIT HELPER FUNCTIONS
;==============================================================================
[bits 16]

; print_string_16: Print null-terminated string via BIOS INT 10h
;   SI = string address
print_string_16:
    push ax
    push si
    mov ah, 0x0E
.loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    pop si
    pop ax
    ret

; check_a20: Test if A20 gate is enabled
;   Returns: ZF=0 (NZ) if enabled, ZF=1 (Z) if disabled
check_a20:
    pushf
    push ds
    push es
    push di
    push si
    cli

    xor ax, ax
    mov ds, ax
    mov si, 0x7DFF

    mov ax, 0xFFFF
    mov es, ax
    mov di, 0x7E10

    mov al, [ds:si]
    push ax
    mov al, [es:di]
    push ax

    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF

    mov al, [ds:si]
    cmp al, 0xFF                ; If wrapped, A20 is off

    pop ax
    mov [es:di], al
    pop ax
    mov [ds:si], al

    sti
    pop si
    pop di
    pop es
    pop ds
    popf
    ret

; a20_keyboard: Enable A20 via keyboard controller
a20_keyboard:
    cli
    call .wait_in
    mov al, 0xAD                ; Disable keyboard
    out 0x64, al

    call .wait_in
    mov al, 0xD0                ; Read output port
    out 0x64, al

    call .wait_out
    in al, 0x60
    push ax

    call .wait_in
    mov al, 0xD1                ; Write output port
    out 0x64, al

    call .wait_in
    pop ax
    or al, 2                    ; Set A20 bit
    out 0x60, al

    call .wait_in
    mov al, 0xAE                ; Re-enable keyboard
    out 0x64, al

    call .wait_in
    sti
    ret

.wait_in:
    in al, 0x64
    test al, 2
    jnz .wait_in
    ret

.wait_out:
    in al, 0x64
    test al, 1
    jz .wait_out
    ret

; a20_fast: Enable A20 via System Control Port A (port 0x92)
a20_fast:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

;==============================================================================
; 64-BIT LONG MODE ENTRY
;==============================================================================
[bits 64]

long_mode_entry:
    ; Set up segment registers with 64-bit data segment
    mov ax, 0x10                ; Kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up temporary stack
    mov rsp, 0x7C00
    mov rbp, rsp

    ; Debug: Write to serial port COM1
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al
    mov al, 0x0D
    out dx, al
    mov al, 0x0A
    out dx, al

    ; Copy kernel from load buffer (0x8200) to final address (0x100000)
    ; Kernel was loaded below 1MB because INT 13h AH=42h has limited
    ; support for buffer addresses above 1MB on some BIOS implementations.
    mov rsi, 0x8200              ; Source: load buffer
    mov rdi, 0x100000            ; Dest:   kernel final address
    mov rcx, 137 * 512 / 8      ; Size:   137 sectors = 70144B = 8768 qwords
    cld
    rep movsq

    ; Debug: Write '+' to serial port after copy
    mov dx, 0x3F8
    mov al, '+'
    out dx, al
    mov al, 0x0D
    out dx, al
    mov al, 0x0A
    out dx, al

    ; Jump to kernel entry point at 0x100000
    ; The kernel's boot.asm clears BSS, sets up stack, and calls kernel_main
    mov rax, 0x100000
    jmp rax

    ; Kernel entry should never return; if it does, halt
    cli
    hlt
    jmp $

;==============================================================================
; DATA SECTION
;==============================================================================

msg_banner:        db 13, 10, "ARES OS Stage 2 Bootloader", 13, 10, 0
msg_loading_kernel: db "Loading kernel... ", 0
msg_kernel_ok:     db "OK", 13, 10, 0
msg_disk_err:      db "FAILED", 13, 10, 0
msg_press_key:     db "Press any key to retry...", 13, 10, 0
msg_bad_kernel:    db "FATAL: Invalid kernel image.", 13, 10, 0
msg_a20:           db "Enabling A20 gate... ", 0
msg_a20_ok:        db "OK", 13, 10, 0
msg_a20_fail:      db "FATAL: A20 gate cannot be enabled.", 13, 10, 0
msg_gdt:           db "Loading GDT... OK", 13, 10, 0
msg_paging:        db "Setting up paging... OK", 13, 10, 0

boot_drive:        db 0

;==============================================================================
; GLOBAL DESCRIPTOR TABLE (GDT)
;==============================================================================
align 8
gdt_start:
    ; Null descriptor (selector 0x00)
    dq 0

    ; 64-bit Kernel Code (selector 0x08): Code, Execute/Read, Long Mode
    dw 0x0000       ; Limit[15:0]
    dw 0x0000       ; Base[15:0]
    db 0x00         ; Base[23:16]
    db 0x9A         ; P=1, DPL=0, S=1, Type=Code+ER
    db 0xA0         ; L=1, G=1, D=0
    db 0x00         ; Base[31:24]

    ; 64-bit Kernel Data (selector 0x10): Data, Read/Write
    dw 0x0000       ; Limit[15:0]
    dw 0x0000       ; Base[15:0]
    db 0x00         ; Base[23:16]
    db 0x92         ; P=1, DPL=0, S=1, Type=Data+RW
    db 0xA0         ; L=1, G=1
    db 0x00         ; Base[31:24]

    ; 32-bit Compatibility Code (selector 0x18): Code, Execute/Read, 32-bit
    dw 0xFFFF       ; Limit[15:0]
    dw 0x0000       ; Base[15:0]
    db 0x00         ; Base[23:16]
    db 0x9A         ; P=1, DPL=0, S=1, Type=Code+ER
    db 0xCF         ; G=1, D=1, L=0
    db 0x00         ; Base[31:24]

    ; 32-bit Compatibility Data (selector 0x20): Data, Read/Write, 32-bit
    dw 0xFFFF       ; Limit[15:0]
    dw 0x0000       ; Base[15:0]
    db 0x00         ; Base[23:16]
    db 0x92         ; P=1, DPL=0, S=1, Type=Data+RW
    db 0xCF         ; G=1, D=1
    db 0x00         ; Base[31:24]
gdt_end:

; GDT Register (for LGDT instruction)
gdtr:
    dw gdt_end - gdt_start - 1  ; Limit (size - 1)
    dq gdt_start                ; Base address

;==============================================================================
; DISK ADDRESS PACKET (DAP) for INT 13h AH=42h
;==============================================================================
; Read kernel from LBA 65 to physical 0x100000.
;
; DAP Structure:
;   Byte 0:    DAP size (16)
;   Byte 1:    Reserved (0)
;   Byte 2-3:  Sectors to read
;   Byte 4-5:  Buffer offset (within segment)
;   Byte 6-7:  Buffer segment
;   Byte 8-15: LBA start sector (64-bit)
;
    ; Load to buffer at 0x8200 (above stage2 at 0x7E00-0x8190).
    ; Kernel will be copied to 0x100000 from long mode.
    ; Kernel will be copied to 0x100000 from long mode.
    ; physical = segment * 16 + offset
    ; 0x0820 * 16 + 0x0000 = 0x8200
align 16
dap_kernel:
    db 0x10                     ; DAP size
    db 0x00                     ; Reserved
    dw 127                      ; Sectors (INT 13h limit: ~127 per call)
    dw 0x0000                   ; Buffer offset
    dw 0x0820                   ; Buffer segment (-> 0x8200, above stage2)
    dq 65                       ; LBA start

; Second DAP: remaining 10 sectors (kernel ~70KB, 137 sectors total)
; Buffer: 0x8200 + 127*512 = 0x18000, LBA: 65 + 127 = 192
; Physical = segment * 16 + offset → 0x1800 * 16 + 0 = 0x18000
align 16
dap_kernel2:
    db 0x10                     ; DAP size
    db 0x00                     ; Reserved
    dw 10                       ; Sectors (remainder)
    dw 0x0000                   ; Buffer offset
    dw 0x1800                   ; Buffer segment (-> 0x18000 = 0x8200 + 127*512)
    dq 192                      ; LBA start (65 + 127)

; No padding needed - dd writes to 512-byte sector boundaries
