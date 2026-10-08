; ============================================================================
; ramtest.s - Comprueba si $3E00-$3FFF es RAM escribible (test aparte).
; ============================================================================
; NO forma parte del juego. Es un binario de diagnostico que se carga en $0800
; y reporta por UART que direcciones del rango $3E00-$3FFF son RAM real.
;
; Metodo: por cada direccion, escribe dos patrones distintos y los relee. Si
; se conservan (y no coinciden con un valor fijo de bus/IO), es RAM.
;
; Usa la ROM API del monitor: uart_init=$BF15, uart_puts=$BF1E, uart_putc=$BF18.
; ============================================================================

        .export _init
        .export __STARTUP__ : absolute = 1

        .segment "ZEROPAGE"
ztmp:       .res 1
ptr_lo:     .res 1
ptr_hi:     .res 1

        .segment "CODE"

ROM_UART_INIT = $BF15
ROM_UART_PUTS = $BF1E
ROM_UART_PUTC = $BF18

_init:
        sei
        cld
        ldx #$FF
        txs

        jsr ROM_UART_INIT
        lda #<msg_title
        ldx #>msg_title
        jsr ROM_UART_PUTS

        ; --- Probar cada direccion de interes ---
        lda #$3E
        ldx #$00
        jsr test_addr
        lda #$3F
        ldx #$00
        jsr test_addr
        lda #$3F
        ldx #$80
        jsr test_addr
        lda #$3F
        ldx #$C0
        jsr test_addr
        lda #$3F
        ldx #$E0
        jsr test_addr
        lda #$3F
        ldx #$F0
        jsr test_addr
        lda #$3F
        ldx #$F8
        jsr test_addr
        lda #$3F
        ldx #$FC
        jsr test_addr
        lda #$3F
        ldx #$FE
        jsr test_addr
        lda #$3F
        ldx #$FF
        jsr test_addr

        lda #<msg_done
        ldx #>msg_done
        jsr ROM_UART_PUTS

loop:
        jmp loop

; ----------------------------------------------------------------------------
; test_addr: A = hi, X = lo de la direccion a probar. Reporta OK/BAD por UART.
;   Escribe $55, lee; escribe $AA, lee. Si lee $55 y luego $AA -> RAM.
; ----------------------------------------------------------------------------
test_addr:
        sta ptr_hi
        stx ptr_lo

        ; imprimir "addr $XXYY: "
        lda #<msg_addr
        ldx #>msg_addr
        jsr ROM_UART_PUTS
        lda ptr_hi
        jsr put_hex
        lda ptr_lo
        jsr put_hex
        lda #<msg_colon
        ldx #>msg_colon
        jsr ROM_UART_PUTS

        ; escribir $55
        ldy #0
        lda #$55
        sta (ptr_lo),y
        ; leer
        lda (ptr_lo),y
        cmp #$55
        bne @bad

        ; escribir $AA
        lda #$AA
        sta (ptr_lo),y
        ; leer
        lda (ptr_lo),y
        cmp #$AA
        bne @bad

        lda #<msg_ok
        ldx #>msg_ok
        jsr ROM_UART_PUTS
        rts
@bad:
        lda #<msg_bad
        ldx #>msg_bad
        jsr ROM_UART_PUTS
        rts

; ----------------------------------------------------------------------------
; put_hex: imprime A como 2 digitos hex.
; ----------------------------------------------------------------------------
put_hex:
        pha
        lsr a
        lsr a
        lsr a
        lsr a
        jsr put_nib
        pla
        and #$0F
        jsr put_nib
        rts
put_nib:
        cmp #10
        bcc @d
        adc #$06               ; carry ya puesto por cmp>=10
@d:
        adc #'0'
        jmp ROM_UART_PUTC      ; imprime y retorna

        .segment "RODATA"
msg_title:      .byte $0D,$0A,"RAM TEST $3E00-$3FFF",$0D,$0A,0
msg_addr:       .byte "addr $",0
msg_colon:      .byte ": ",0
msg_ok:         .byte "RAM",$0D,$0A,0
msg_bad:        .byte "no-ram",$0D,$0A,0
msg_done:       .byte "done",$0D,$0A,0

        .segment "BSS"
