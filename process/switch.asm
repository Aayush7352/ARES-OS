;==============================================================================
; ARES OS - Context Switch (x86_64)
;==============================================================================
; Implements cooperative multitasking context switch.
; Saves all callee-saved registers of current process, switches stack,
; restores next process's registers, and returns.
;
; C declaration:
;   void context_switch(pcb_t *next);
;
; The PCB's rsp field points to a save area with this layout (top to bottom):
;   [rbp]     <- RSP points here after save
;   [rbx]
;   [r12]
;   [r13]
;   [r14]
;   [r15]
;   [ret_addr] <- 'ret' jumps here
;==============================================================================

[bits 64]
[global context_switch]

section .text

context_switch:
    ; Save current process context
    ; Callee-saved registers: rbx, rbp, r12-r15
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Store current RSP into current process PCB
    ; current_process is a global pointer to pcb_t
    ; pcb_t.rsp is at offset 32 (after pid[4], state[4], name[24])
    extern current_process
    mov rax, [current_process]
    test rax, rax
    jz .switch_direct
    mov [rax + 40], rsp      ; Save RSP into current->rsp (offset 40)

.switch_direct:
    ; Load next process RSP from PCB (passed in rdi)
    mov rsp, [rdi + 40]      ; next->rsp (offset 40)

    ; Restore callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; Return to where next process was executing
    ret
