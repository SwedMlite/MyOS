%macro ISR_NO_ERROR_CODE_STUB 1
    global isr%1
isr%1:
    cli
    push byte 0        ; dummy error code for uniform stack
    push byte %1       ; actual interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_WITH_ERROR_CODE_STUB 1
    global isr%1
isr%1:
    cli
    push byte %1       ; interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

ISR_NO_ERROR_CODE_STUB 0 ;  0: Divide By Zero Exception

ISR_NO_ERROR_CODE_STUB 1 ;  1: Debug Exception

ISR_NO_ERROR_CODE_STUB 2 ;  2: Non Maskable Interrupt Exception

ISR_NO_ERROR_CODE_STUB 3 ;  3: Breakpoint Exception

ISR_NO_ERROR_CODE_STUB 4 ;  4: Into Detected Overflow Exception

ISR_NO_ERROR_CODE_STUB 5 ;  5: Out of Bounds Exception

ISR_NO_ERROR_CODE_STUB 6 ;  6: Invalid Opcode Exception

ISR_NO_ERROR_CODE_STUB 7 ;  7: No Coprocessor Exception

ISR_WITH_ERROR_CODE_STUB 8 ;  8: Double Fault Exception (With Error Code!)

ISR_NO_ERROR_CODE_STUB 9 ;  9: Coprocessor Segment Overrun Exception

ISR_WITH_ERROR_CODE_STUB 10 ; 10: Bad TSS Exception (With Error Code!)

ISR_WITH_ERROR_CODE_STUB 11 ; 11: Segment Not Present Exception (With Error Code!)

ISR_WITH_ERROR_CODE_STUB 12 ; 12: Stack Fault Exception (With Error Code!)

ISR_WITH_ERROR_CODE_STUB 13 ; 13: General Protection Fault Exception (With Error Code!)

ISR_WITH_ERROR_CODE_STUB 14 ; 14: Page Fault Exception (With Error Code!)

ISR_NO_ERROR_CODE_STUB 15 ; 15: Unknown Interrupt Exception

ISR_NO_ERROR_CODE_STUB 16 ; 16: Coprocessor Fault Exception

ISR_NO_ERROR_CODE_STUB 17 ; 17: Alignment Check Exception (486+)

ISR_NO_ERROR_CODE_STUB 18 ; 18: Machine Check Exception (Pentium/586+)

ISR_NO_ERROR_CODE_STUB 19 ; 19: Reserved Exception

ISR_NO_ERROR_CODE_STUB 20 ; 20: Reserved Exception

ISR_NO_ERROR_CODE_STUB 21 ; 21: Reserved Exception

ISR_NO_ERROR_CODE_STUB 22 ; 22: Reserved Exception

ISR_NO_ERROR_CODE_STUB 23 ; 23: Reserved Exception

ISR_NO_ERROR_CODE_STUB 24 ; 24: Reserved Exception

ISR_NO_ERROR_CODE_STUB 25 ; 25: Reserved Exception

ISR_NO_ERROR_CODE_STUB 26 ; 26: Reserved Exception

ISR_NO_ERROR_CODE_STUB 27 ; 27: Reserved Exception

ISR_NO_ERROR_CODE_STUB 28 ; 28: Reserved Exception

ISR_NO_ERROR_CODE_STUB 29 ; 29: Reserved Exception

ISR_NO_ERROR_CODE_STUB 30 ; 30: Reserved Exception

ISR_NO_ERROR_CODE_STUB 31 ; 31: Reserved Exception


; We call a C function in here. We need to let the assembler know
; that 'fault_handler' exists in another file
extern fault_handler

; This is our common ISR stub. It saves the processor state, sets
; up for kernel mode segments, calls the C-level fault handler,
; and finally restores the stack frame.
isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10   ; Load the Kernel Data Segment descriptor!
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp   ; Push us the stack
    push eax
    mov eax, fault_handler
    call eax       ; A special call, preserves the 'eip' register
    pop eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8     ; Cleans up the pushed error code and pushed ISR number
    iret           ; pops 5 things at once: CS, EIP, EFLAGS, SS, and ESP!