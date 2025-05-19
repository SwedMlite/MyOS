%macro IRQ_HANDLER_STUB 2
    global irq%1
irq%1:
    cli
    push byte 0
    push byte %2
    jmp irq_common_stub

%endmacro

IRQ_HANDLER_STUB 0, 32  ; IRQ0 -> INT 32
IRQ_HANDLER_STUB 1, 33  ; IRQ1 -> INT 33
IRQ_HANDLER_STUB 2, 34  ; IRQ2 -> INT 34
IRQ_HANDLER_STUB 3, 35  ; IRQ2 -> INT 35
IRQ_HANDLER_STUB 4, 36  ; IRQ2 -> INT 36
IRQ_HANDLER_STUB 5, 37  ; IRQ2 -> INT 37
IRQ_HANDLER_STUB 6, 38  ; IRQ2 -> INT 38
IRQ_HANDLER_STUB 7, 39  ; IRQ2 -> INT 39
IRQ_HANDLER_STUB 8, 40  ; IRQ2 -> INT 40
IRQ_HANDLER_STUB 9, 41  ; IRQ2 -> INT 41
IRQ_HANDLER_STUB 10, 42  ; IRQ2 -> INT 42
IRQ_HANDLER_STUB 11, 43  ; IRQ2 -> INT 43
IRQ_HANDLER_STUB 12, 44  ; IRQ2 -> INT 44
IRQ_HANDLER_STUB 13, 45  ; IRQ2 -> INT 45
IRQ_HANDLER_STUB 14, 46  ; IRQ2 -> INT 46
IRQ_HANDLER_STUB 15, 47 ; IRQ15 -> INT 47

extern irq_handler

; This is a stub that we have created for IRQ based ISRs. This calls
; 'irq_handler' in our C code. We need to create this in an 'irq.c'
irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp
    push eax
    mov eax, irq_handler
    call eax
    pop eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret