;==============================================================================
; ARES OS - Interrupt Service Routine Stubs (x86_64)
;==============================================================================
; Provides ISR stubs for all 256 interrupt vectors. Each stub maintains the
; invariant that the stack contains: error_code, int_no, then the CPU-pushed
; RIP/CS/RFLAGS.  The common handler (isr_common) saves all registers and
; calls the C function isr_handler(interrupt_frame_t *).
;==============================================================================

[bits 64]

; External C handler
[extern isr_handler]

; Scheduler reschedule mechanism
[extern yield]
[extern scheduler_need_resched]

;==============================================================================
; ISR Stub Macros
;==============================================================================

; ISR without hardware error code — push a dummy 0, then the vector number
%macro ISR_NOERR 1
[global isr_%1]
isr_%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

; ISR with hardware error code — CPU already pushed it, just push vector
%macro ISR_ERR 1
[global isr_%1]
isr_%1:
    push qword %1
    jmp isr_common
%endmacro

;==============================================================================
; CPU Exception Stubs (Vectors 0-31)
;==============================================================================
ISR_NOERR 0      ; Division By Zero
ISR_NOERR 1      ; Debug
ISR_NOERR 2      ; Non-Maskable Interrupt
ISR_NOERR 3      ; Breakpoint
ISR_NOERR 4      ; Overflow
ISR_NOERR 5      ; Bound Range Exceeded
ISR_NOERR 6      ; Invalid Opcode
ISR_NOERR 7      ; Device Not Available
ISR_ERR    8     ; Double Fault (has error code)
ISR_NOERR 9      ; Coprocessor Segment Overrun (reserved)
ISR_ERR    10    ; Invalid TSS (has error code)
ISR_ERR    11    ; Segment Not Present (has error code)
ISR_ERR    12    ; Stack-Segment Fault (has error code)
ISR_ERR    13    ; General Protection Fault (has error code)
ISR_ERR    14    ; Page Fault (has error code)
ISR_NOERR 15     ; Reserved
ISR_NOERR 16     ; x87 FPU Floating-Point Error
ISR_ERR    17    ; Alignment Check (has error code)
ISR_NOERR 18     ; Machine Check
ISR_NOERR 19     ; SIMD Floating-Point Exception
ISR_NOERR 20     ; Virtualization Exception
ISR_NOERR 21     ; Reserved
ISR_NOERR 22     ; Reserved
ISR_NOERR 23     ; Reserved
ISR_NOERR 24     ; Reserved
ISR_NOERR 25     ; Reserved
ISR_NOERR 26     ; Reserved
ISR_NOERR 27     ; Reserved
ISR_NOERR 28     ; Reserved
ISR_NOERR 29     ; Reserved
ISR_NOERR 30     ; Reserved (Security Exception on some CPUs)
ISR_NOERR 31     ; Reserved

;==============================================================================
; User-defined Interrupt Stubs (Vectors 32-255)
;==============================================================================
%assign i 32
%rep 224
ISR_NOERR i
%assign i i+1
%endrep

;==============================================================================
; Common ISR Handler
;==============================================================================
; Saves all general-purpose registers, calls the C handler, restores, returns.
;
; Stack layout at entry (top = lowest address):
;   [r15..rax]     — pushed by isr_common (15 × 8 = 120 bytes)
;   [int_no]       — pushed by stub (8 bytes)
;   [err_code]     — pushed by stub/CPU (8 bytes)
;   [rip, cs, rflags] — pushed by CPU (3 × 8 = 24 bytes)
;
; The C handler receives a pointer to this register frame.
;==============================================================================
[global isr_common]
isr_common:
    ; Save all general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Call C handler with frame pointer in RDI (first argument)
    mov rdi, rsp
    call isr_handler

    ; Check reschedule flag — preemptive scheduling hook
    cmp dword [scheduler_need_resched], 0
    je .restore_and_iret

    ; Clear the flag and yield to next process
    mov dword [scheduler_need_resched], 0
    call yield

.restore_and_iret:

    ; Restore all general-purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove int_no and err_code from stack
    add rsp, 16

    ; Return from interrupt
    iretq

;==============================================================================
; ISR Stub Address Table
;==============================================================================
; Lookup table indexed by vector number so the C code can populate the IDT.
;==============================================================================
[global isr_stub_table]
isr_stub_table:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i+1
%endrep