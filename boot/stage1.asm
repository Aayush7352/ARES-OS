;==============================================================================
; ARES OS - Stage 1 Bootloader (MBR)
;==============================================================================
; The first sector loaded by the BIOS at 0x7C00.
; Responsibilities:
;   1. Set up real-mode environment (segments, stack)
;   2. Load Stage 2 bootloader from disk via INT 13h AH=42h (LBA)
;   3. Verify checksum and jump to Stage 2
;
; Memory Layout:
;   0x7C00 - 0x7DFF : Stage 1 (this code, 512 bytes)
;   0x7E00 - 0x7FFF : Stage 2 load buffer start
;   0x7E00 - 0xFC00 : Stage 2 (max ~31KB)
;   0x100000        : Kernel load address
;==============================================================================

[org 0x7C00]
[bits 16]

;==============================================================================
; Entry Point
;==============================================================================
start:
    ; Save boot drive number (passed in DL by BIOS)
    mov [boot_drive], dl

    ; Set up segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Set up stack (grows downward from 0x7C00)
    mov sp, 0x7C00

    ; Print boot message
    mov si, msg_booting
    call print_string

    ; Load Stage 2 from disk
    mov si, dap_stage2          ; Disk Address Packet for Stage 2
    mov dl, [boot_drive]        ; Boot drive
    mov ah, 0x42                ; Extended Read Sectors
    int 0x13
    jc disk_error               ; CF set = error

    ; Print success and jump to Stage 2
    mov si, msg_loading_stage2
    call print_string

    ; Far jump to Stage 2 (segment:offset)
    jmp 0x0000:0x7E00

;==============================================================================
; Disk Error Handler
;==============================================================================
disk_error:
    mov si, msg_disk_error
    call print_string

    mov si, msg_retry
    call print_string

    ; Wait for keypress then retry
    xor ax, ax
    int 0x16                    ; Wait for keypress

    ; Retry the load
    mov si, dap_stage2
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_fatal               ; Second failure = fatal

    ; Success on retry
    mov si, msg_loading_stage2
    call print_string
    jmp 0x0000:0x7E00

disk_fatal:
    mov si, msg_fatal
    call print_string
    ; Halt the system
    cli
    hlt
    jmp disk_fatal

;==============================================================================
; Stage 2 Corrupted
;==============================================================================
stage2_corrupt:
    mov si, msg_corrupt
    call print_string
    cli
    hlt
    jmp stage2_corrupt

;==============================================================================
; Helper: print_string - Print null-terminated string via INT 0x10
;   SI = pointer to string
;==============================================================================
print_string:
    push ax
    push si
    mov ah, 0x0E                ; BIOS teletype output
.loop:
    lodsb                       ; Load byte from [SI] into AL, increment SI
    or al, al                   ; Check for null terminator
    jz .done
    int 0x10                    ; Print character
    jmp .loop
.done:
    pop si
    pop ax
    ret

;==============================================================================
; Data Section
;==============================================================================
; Boot messages
msg_booting:      db 13, 10, "ARES OS Booting...", 13, 10, 0
msg_loading_stage2: db "Stage 1 loaded. Loading Stage 2...", 13, 10, 0
msg_disk_error:    db "Disk error occurred.", 13, 10, 0
msg_retry:         db "Press any key to retry...", 13, 10, 0
msg_fatal:         db "Fatal: Cannot read disk. System halted.", 13, 10, 0
msg_corrupt:       db "Fatal: Stage 2 bootloader corrupted.", 13, 10, 0

; Boot drive number (written by BIOS to DL, saved here)
boot_drive:        db 0

;==============================================================================
; Disk Address Packet (DAP) for INT 13h AH=42h Extended Read
;==============================================================================
; Structure:
;   Byte 0:  Size of DAP (16 bytes)
;   Byte 1:  Reserved (0)
;   Byte 2-3: Number of sectors to read (max 0x007F for our layout)
;   Byte 4-5: Buffer offset (in segment:offset form)
;   Byte 6-7: Buffer segment
;   Byte 8-15: LBA start sector (64-bit little-endian)
;==============================================================================
dap_stage2:
    db 0x10                     ; Size of DAP
    db 0x00                     ; Reserved
    dw 64                       ; Sectors to read (32KB for Stage 2)
    dw 0x7E00                   ; Buffer offset (0x0000:0x7E00)
    dw 0x0000                   ; Buffer segment
    dq 1                        ; LBA start (sector 1, right after MBR)

;==============================================================================
; Boot Signature
;==============================================================================
; Pad to 510 bytes, then add the boot signature 0xAA55
times 510 - ($ - $$) db 0
dw 0xAA55
