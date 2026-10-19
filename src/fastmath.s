; ---------------------------------------------------------------------------
; fastmath.s - fixed point helpers for the renderer.
;
; SDCC's generic 32 bit helpers are extremely expensive on a Z80: __mullong
; costs ~3600 cycles and __divslong ~10800, and the perspective transform runs
; them several times per wall.  These replacements do exactly the two
; operations the renderer needs and nothing else.
;
; Calling convention is SDCC __sdcccall(1): first 16 bit argument in HL,
; second in DE, 16 bit result returned in DE.
; ---------------------------------------------------------------------------
        .module fastmath
        .globl _MulShift8
        .globl _MulDivS
        .globl _PsgPeriod
        .globl _Div16Shift8
        .globl _mdMul

        .area _DATA
_mdMul:         .ds 2           ; multiplier for MulDivS (a constant per site)
fm_sign:        .ds 1
fm_lo:          .ds 2
fm_hi:          .ds 2

        .area _CODE

; ---------------------------------------------------------------------------
; s16 MulShift8 (s16 a, s16 b)   ->   (a * b) >> 8
; Exact: the full signed 32 bit product is formed and bits 8..23 returned,
; which matches  (int16_t)(((int32_t)a * b) >> 8)  including for negatives.
; ---------------------------------------------------------------------------
_MulShift8::
        ld      a, h
        xor     a, d
        ld      (fm_sign), a            ; bit 7 = result is negative

        call    fm_abs_hl_de            ; |HL|, |DE|
        call    fm_mul16                ; DE:HL = HL * DE  (unsigned)

        ld      a, (fm_sign)
        and     a, #0x80
        jr      Z, ms_pos
        ; negate the 32 bit value DE:HL
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        ld      a, e
        cpl
        ld      e, a
        ld      a, d
        cpl
        ld      d, a
        inc     l
        jr      NZ, ms_pos
        inc     h
        jr      NZ, ms_pos
        inc     e
        jr      NZ, ms_pos
        inc     d
ms_pos:
        ; result = bits 8..23  ->  low byte = H, high byte = E
        ld      a, e
        ld      d, a
        ld      a, h
        ld      e, a
        ret

; ---------------------------------------------------------------------------
; s16 MulDivS (s16 a, s16 d)   ->   (a * mdMul) / d      (truncated toward 0)
; mdMul is an unsigned 16 bit constant written by the caller.
; ---------------------------------------------------------------------------
_MulDivS::
        ld      a, h
        xor     a, d
        ld      (fm_sign), a

        ; |d| -> fm_lo (the multiplier sign is folded in below)
        bit     7, d
        jr      Z, md_dpos
        ld      a, #0
        sub     a, e
        ld      e, a
        ld      a, #0
        sbc     a, d
        ld      d, a
md_dpos:
        ld      (fm_lo), de             ; divisor

        ; |a| -> HL
        bit     7, h
        jr      Z, md_apos
        ld      a, #0
        sub     a, l
        ld      l, a
        ld      a, #0
        sbc     a, h
        ld      h, a
md_apos:
        ld      de, (_mdMul)
        bit     7, d
        jr      Z, md_mpos
        ld      a, (fm_sign)
        xor     a, #0x80
        ld      (fm_sign), a
        ld      a, #0
        sub     a, e
        ld      e, a
        ld      a, #0
        sbc     a, d
        ld      d, a
md_mpos:
        call    fm_mul16                ; DE:HL = |a| * |mdMul|

        ld      bc, (fm_lo)             ; divisor
        call    fm_div3216              ; HL = quotient

        ld      a, (fm_sign)
        and     a, #0x80
        jr      Z, md_done
        ld      a, #0
        sub     a, l
        ld      l, a
        ld      a, #0
        sbc     a, h
        ld      h, a
md_done:
        ex      de, hl                  ; result in DE
        ret

; ---------------------------------------------------------------------------
; u16 PsgPeriod (u16 freq)   ->   3579545 / (32 * freq)   ==   111861 / freq
;
; This one runs inside the sound interrupt, so it must be re-entrant: it uses
; registers only (fm_div3216 is fully unrolled and touches no memory).  The
; obvious `111861UL / freq` in C calls SDCC's __divulong, which keeps state in
; static scratch and silently corrupts whatever 32 bit division the main loop
; happened to be in the middle of.
; ---------------------------------------------------------------------------
_PsgPeriod::
        ld      b, h
        ld      c, l                    ; BC = freq
        ld      hl, #0xB4F5
        ld      de, #0x0001             ; DE:HL = 111861
        call    fm_div3216
        ex      de, hl                  ; quotient -> DE (return register)
        ret

; ---------------------------------------------------------------------------
; u16 Div16Shift8 (u16 a, u16 d)   ->   (a << 8) / d
; The caller guarantees (a >> 8) < d, so the quotient fits in 16 bits.
; HL = a, DE = d, result returned in DE.
; ---------------------------------------------------------------------------
_Div16Shift8::
        ld      b, d
        ld      c, e                    ; BC = divisor
        ld      a, h
        ld      h, l
        ld      l, #0                   ; HL = (a & 0xff) << 8
        ld      d, #0
        ld      e, a                    ; DE = a >> 8
        call    fm_div3216
        ex      de, hl                  ; quotient -> DE (return register)
        ret

; ---------------------------------------------------------------------------
; helpers
; ---------------------------------------------------------------------------
; |HL| and |DE|
fm_abs_hl_de:
        bit     7, h
        jr      Z, ab_1
        ld      a, #0
        sub     a, l
        ld      l, a
        ld      a, #0
        sbc     a, h
        ld      h, a
ab_1:
        bit     7, d
        ret     Z
        ld      a, #0
        sub     a, e
        ld      e, a
        ld      a, #0
        sbc     a, d
        ld      d, a
        ret

; unsigned 16x16 -> 32.  in: HL, DE.  out: DE:HL (DE high, HL low).  uses BC, A
; The shifted operand lives in DE, so whichever operand fits in 8 bits is put
; there and only 8 iterations are run - that is the common case here (the sine
; table is +/-255 and texture coordinates are <= 128).
fm_mul16:
        ld      a, h
        or      a, a
        jr      NZ, fm_try_de
        ; HL < 256 -> multiplier = L, multiplicand = DE
        ld      b, d
        ld      c, e
        ld      d, l
        ld      e, #0
        ld      hl, #0
        ld      a, #8
        jr      fm_m1
fm_try_de:
        ld      a, d
        or      a, a
        jr      NZ, fm_full
        ; DE < 256 -> multiplier = E, multiplicand = HL
        ld      b, h
        ld      c, l
        ld      d, e
        ld      e, #0
        ld      hl, #0
        ld      a, #8
        jr      fm_m1
fm_full:
        ld      b, h
        ld      c, l                    ; BC = multiplicand
        ld      hl, #0
        ld      a, #16
fm_m1:
        add     hl, hl
        rl      e
        rl      d
        jr      NC, fm_m2
        add     hl, bc
        jr      NC, fm_m2
        inc     de
fm_m2:
        dec     a
        jr      NZ, fm_m1
        ret

; unsigned 32/16.  in: DE:HL = dividend, BC = divisor.  out: HL = quotient,
; DE = remainder.  The quotient is assumed to fit in 16 bits.
fm_div3216:
        ld      a, b
        or      a, c
        jr      NZ, fm_d0
        ld      hl, #0x7fff             ; divide by zero -> saturate
        ret
fm_d0:
        ; fully unrolled - there is no spare register for a loop counter and a
        ; memory counter would cost more than the 320 bytes of code
        .rept   16
        add     hl, hl
        rl      e
        rl      d
        ld      a, e
        sub     a, c
        ld      a, d
        sbc     a, b
        jr      C, .+9
        ld      a, e
        sub     a, c
        ld      e, a
        ld      a, d
        sbc     a, b
        ld      d, a
        inc     l
        .endm
        ret
